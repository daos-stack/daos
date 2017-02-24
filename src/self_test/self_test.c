/* Copyright (C) 2016-2017 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <getopt.h>
#include <string.h>
#include <math.h>

#include <crt_api.h>
#include <crt_util/common.h>

/*
 * TODO: DELETE REGION BEGIN
 * The code between these two DELETE REGION blocks is duplicated from
 * headers reachable if crt_internal.h is included. Since this application can't
 * yet include crt_internal (because it is a separate app), these need to be
 * duplicated here. Everything in this region needs to be deleted when the
 * self-test client is integrated into CART and replaced with a call to
 * #include <crt_internal.h>
 */
#define CRT_OPC_SELF_TEST_BOTH_EMPTY		0xFFFF0200U
#define CRT_OPC_SELF_TEST_SEND_EMPTY_REPLY_IOV	0xFFFF0201U
#define CRT_OPC_SELF_TEST_SEND_IOV_REPLY_EMPTY	0xFFFF0202U
#define CRT_OPC_SELF_TEST_BOTH_IOV		0xFFFF0203U
#define CRT_OPC_SELF_TEST_SEND_BULK_REPLY_IOV	0xFFFF0204U
#define CRT_OPC_SELF_TEST_SEND_IOV_REPLY_BULK	0xFFFF0205U
#define CRT_OPC_SELF_TEST_BOTH_BULK		0xFFFF0206U
#define CRT_OPC_SELF_TEST_OPEN_SESSION		0xFFFF0210U
#define CRT_OPC_SELF_TEST_CLOSE_SESSION		0xFFFF0211U

#define CRT_SELF_TEST_MAX_MSG_SIZE		0x40000000U
#define CRT_SELF_TEST_AUTO_BULK_THRESH		(1 << 20)

#define ISBULK(type) (type == CRT_SELF_TEST_MSG_TYPE_BULK_GET || \
		      type == CRT_SELF_TEST_MSG_TYPE_BULK_PUT)

#define CRT_BULK_NULL            (NULL)

/*
 * RPC argument structures
 *
 * Push pragma pack=1 onto the stack for pragma pack. This forces these
 * structures to be packed together without any padding between members
 */
#pragma pack(push, 1)
enum crt_st_msg_type {
	CRT_SELF_TEST_MSG_TYPE_EMPTY = 0,
	CRT_SELF_TEST_MSG_TYPE_IOV,
	CRT_SELF_TEST_MSG_TYPE_BULK_PUT,
	CRT_SELF_TEST_MSG_TYPE_BULK_GET,
};
struct crt_st_session_params {
	uint32_t send_size;
	uint32_t reply_size;
	uint32_t num_buffers;
	union {
		struct {
			enum crt_st_msg_type send_type: 2;
			enum crt_st_msg_type reply_type: 2;
		};
		uint32_t flags;
	};
};
struct crt_st_send_id_iov {
	int32_t session_id;
	crt_iov_t buf;
};
struct crt_st_send_id_iov_bulk {
	int32_t session_id;
	crt_iov_t buf;
	crt_bulk_t bulk_hdl;
};
struct crt_st_send_id_bulk {
	int32_t session_id;
	crt_bulk_t bulk_hdl;
};
/* Pop pragma pack to restore original struct packing behavior */
#pragma pack(pop)
static inline crt_opcode_t
crt_st_compute_opcode(enum crt_st_msg_type send_type,
		      enum crt_st_msg_type reply_type)
{
	C_ASSERT(send_type >= 0 && send_type < 4);
	C_ASSERT(reply_type >= 0 && reply_type < 4);
	C_ASSERT(send_type != CRT_SELF_TEST_MSG_TYPE_BULK_PUT);
	C_ASSERT(reply_type != CRT_SELF_TEST_MSG_TYPE_BULK_GET);

	crt_opcode_t opcodes[4][4] = { { CRT_OPC_SELF_TEST_BOTH_EMPTY,
					 CRT_OPC_SELF_TEST_SEND_EMPTY_REPLY_IOV,
					 CRT_OPC_SELF_TEST_BOTH_BULK,
					 -1 },
				       { CRT_OPC_SELF_TEST_SEND_IOV_REPLY_EMPTY,
					 CRT_OPC_SELF_TEST_BOTH_IOV,
					 CRT_OPC_SELF_TEST_SEND_IOV_REPLY_BULK,
					 -1 },
				       { -1, -1, -1, -1 },
				       { CRT_OPC_SELF_TEST_BOTH_BULK,
					 CRT_OPC_SELF_TEST_SEND_BULK_REPLY_IOV,
					 CRT_OPC_SELF_TEST_BOTH_BULK,
					 -1 } };

	return opcodes[send_type][reply_type];
}
int crt_validate_grpid(const crt_group_id_t grpid);
int
crt_rpc_reg_internal(crt_opcode_t opc, struct crt_req_format *crf,
		     crt_rpc_cb_t rpc_handler, struct crt_corpc_ops *co_ops);
/*
 * TODO: DELETE REGION END
 */

static const char * const crt_st_msg_type_str[] = { "EMPTY",
						    "IOV",
						    "BULK_PUT",
						    "BULK_GET" };

/* User input maximum values */
#define SELF_TEST_MAX_REPETITIONS (0x40000000)
#define SELF_TEST_MAX_INFLIGHT (0x40000000)
#define SELF_TEST_MAX_LIST_STR_LEN (1 << 16)
#define SELF_TEST_MAX_NUM_ENDPOINTS (UINT32_MAX)

struct st_latency {
	int64_t val;
	uint32_t rank;
	uint32_t tag;
	int cci_rc; /* Return code from the callback */
};

struct st_endpoint {
	uint32_t rank;
	uint32_t tag;

	/* Session ID to use when sending messages to this endpoint */
	int32_t session_id;

	/*
	 * If this endpoint is detected as evicted, no more messages should be
	 * sent to it
	 */
	uint8_t evicted;
};

enum st_fatal_err {
	ST_SUCCESS = 0,
	ST_UNREACH = CER_UNREACH,
	ST_UNKNOWN = CER_UNKNOWN
};

struct st_cb_args {
	crt_context_t			 crt_ctx;
	crt_group_t			*srv_grp;

	/* Target number of RPCs */
	int				 rep_count;

	/* Message size of current RPC workload */
	struct crt_st_session_params	 test_params;

	/*
	 * Used to track how many RPCs have been sent so far
	 * NOTE: Write-protected by cb_args_lock
	 */
	int				 rep_idx;

	/*
	 * Used to track how many RPCs have been handled so far
	 * NOTE: Write-protected by cb_args_lock
	 */
	int				 rep_completed_count;

	/* Used to protect counters in this structure across threads */
	pthread_spinlock_t		 cb_args_lock;

	/* Used to measure individial RPC latencies */
	struct st_latency		*rep_latencies;

	/* List of endpoints to test against */
	struct st_endpoint		*endpts;

	/* Number of endpoints in the endpts array */
	uint32_t			 num_endpts;

	/*
	 * Last used endpoint index
	 * NOTE: Write-protected by cb_args_lock
	 */
	uint32_t			 next_endpt_idx;

	/*
	 * Set to zero initially, marked as nonzero if run_self_test detects
	 * that the test can no longer proceed. For example, if all endpoints
	 * have been evicted or the underlying fabric returns unexpected errors
	 */
	enum st_fatal_err		 fatal_err;
};

/*
 * An instance of this structure exists per inflight RPC to serve as the
 * "private" data for each repetition
 */
struct st_cb_data {
	/*
	 * A pointer to the "public" data that is the same for all repetitions
	 * of this run
	 */
	struct st_cb_args	*cb_args;

	int			 rep_idx;
	struct timespec		 sent_time;
	struct st_endpoint	*endpt;

	crt_bulk_t		 bulk_hdl;
	crt_sg_list_t		 sg_list;
	crt_iov_t		 sg_iov;

	/* Length of the buffer attached to the end of this struct */
	size_t			 buf_len;

	/*
	 * Extra space used for the payload of this repetition
	 *
	 * Size is determined by whether the reply uses BULK or not:
	 * if reply is bulk
	 *   size = max(cb_args->test_params.send_size,
	 *              cb_args->test_params.reply_size)
	 * else
	 *   size = cb_args->test_params.send_size;
	 */
	char			*buf;
};

struct st_open_session_cb_data {
	/*
	 * A pointer to the "public" data that is the same for all repetitions
	 * of this run
	 */
	struct st_cb_args	*cb_args;

	struct st_endpoint	*endpt;
};

/* Global shutdown flag, used to terminate the progress thread */
static int g_shutdown_flag;

/* Forward Declarations */
static int response_cb(const struct crt_cb_info *cb_info);

/*
 * This function sends an RPC to the next available endpoint.
 *
 * If sending fails for any reason, the endpoint is marked as evicted and the
 * function attempts to send to the next endpoint in the list until none remain.
 * This function will only return a non-zero value if there are no remaining
 * endpoints that it is possible to send a message to, or if crt_gettime()
 * fails.
 *
 * skip_inc_complete is a flag that, when set to anything but zero, skips
 * incrementing the rep_completed_count - this is useful when generating the
 * initial RPCs
 */
static int send_next_rpc(struct st_cb_data *cb_data, int skip_inc_complete)
{
	struct st_cb_args	*cb_args = cb_data->cb_args;

	crt_rpc_t		*new_rpc;
	void			*args = NULL;
	crt_endpoint_t		 local_endpt = {.ep_grp = cb_args->srv_grp};
	struct st_endpoint	*endpt_ptr;
	crt_opcode_t		 opcode;
	uint32_t		 failed_endpts;

	int			 local_rep;
	int			 ret;

	/******************** LOCK: cb_args_lock ********************/
	pthread_spin_lock(&cb_args->cb_args_lock);

	/* Only mark completion of an RPC if requested */
	if (skip_inc_complete == 0)
		cb_args->rep_completed_count += 1;

	/* Get an index for a message that still needs to be sent */
	local_rep = cb_args->rep_idx;
	if (cb_args->rep_idx < cb_args->rep_count)
		cb_args->rep_idx += 1;

	pthread_spin_unlock(&cb_args->cb_args_lock);
	/******************* UNLOCK: cb_args_lock *******************/

	/* Only send another message if one is left to send */
	if (local_rep >= cb_args->rep_count)
		return 0;

	/*
	 * Loop until either:
	 * - Detect that no more RPCs need to be sent
	 * - A new RPC message is sent successfully
	 * - All endpoints are marked as evicted and it is impossible to send
	 *   another message
	 * - crt_gettime() fails (which shouldn't happen)
	 *
	 * In each of these cases the relevant code will return without needing
	 * to break the loop
	 */
	while (1) {
		/******************** LOCK: cb_args_lock ********************/
		pthread_spin_lock(&cb_args->cb_args_lock);

		/* Get the next non-evicted endpoint to send a message to */
		failed_endpts = 0;
		do {
			if (failed_endpts >= cb_args->num_endpts) {
				C_ERROR("No non-evicted endpoints remaining\n");
				cb_args->fatal_err = ST_UNREACH;
				return -CER_UNREACH;
			}
			failed_endpts++;

			endpt_ptr = &cb_args->endpts[cb_args->next_endpt_idx];
			cb_args->next_endpt_idx++;
			if (cb_args->next_endpt_idx >= cb_args->num_endpts)
				cb_args->next_endpt_idx = 0;
		} while (endpt_ptr->evicted != 0);

		pthread_spin_unlock(&cb_args->cb_args_lock);
		/******************* UNLOCK: cb_args_lock *******************/

		local_endpt.ep_rank = endpt_ptr->rank;
		local_endpt.ep_tag = endpt_ptr->tag;

		/* Re-use payload data memory, set arguments */
		cb_data->rep_idx = local_rep;

		/*
		 * For the repetition we are just now generating, set which
		 * rank/tag this upcoming latency measurement will be for
		 */
		cb_args->rep_latencies[cb_data->rep_idx].rank =
			local_endpt.ep_rank;
		cb_args->rep_latencies[cb_data->rep_idx].tag =
			local_endpt.ep_tag;

		/*
		 * Determine which opcode (and thus underlying structures)
		 * should be used for this test message
		 */
		opcode = crt_st_compute_opcode(cb_args->test_params.send_type,
					       cb_args->test_params.reply_type);

		/* Start a new RPC request */
		ret = crt_req_create(cb_args->crt_ctx, local_endpt,
				     opcode, &new_rpc);
		if (ret != 0) {
			C_WARN("crt_req_create failed for endpoint=%u:%u;"
			       " ret = %d\n",
			       local_endpt.ep_rank, local_endpt.ep_tag, ret);

			goto try_again;
		}

		C_ASSERTF(new_rpc != NULL,
			  "crt_req_create succeeded but RPC is NULL\n");

		/* No arguments to assemble for BOTH_EMPTY RPCs */
		if (opcode == CRT_OPC_SELF_TEST_BOTH_EMPTY)
			goto send_rpc;

		/* Get the arguments handle */
		args = crt_req_get(new_rpc);
		C_ASSERTF(args != NULL, "crt_req_get returned NULL\n");

		/* Session ID is always the first field */
		*((int32_t *)args) = endpt_ptr->session_id;

		switch (opcode) {
		case CRT_OPC_SELF_TEST_SEND_IOV_REPLY_EMPTY:
		case CRT_OPC_SELF_TEST_BOTH_IOV:
			{
				struct crt_st_send_id_iov *typed_args =
					(struct crt_st_send_id_iov *)args;

				C_ASSERT(cb_data->buf_len >=
					 cb_args->test_params.send_size);
				crt_iov_set(&typed_args->buf,
					    cb_data->buf,
					    cb_args->test_params.send_size);
			}
			break;
		case CRT_OPC_SELF_TEST_SEND_IOV_REPLY_BULK:
			{
				struct crt_st_send_id_iov_bulk *typed_args =
					(struct crt_st_send_id_iov_bulk *)args;

				C_ASSERT(cb_data->buf_len >=
					 cb_args->test_params.send_size);
				crt_iov_set(&typed_args->buf,
					    cb_data->buf,
					    cb_args->test_params.send_size);
				typed_args->bulk_hdl = cb_data->bulk_hdl;
				C_ASSERT(typed_args->bulk_hdl != CRT_BULK_NULL);
			}
			break;
		case CRT_OPC_SELF_TEST_SEND_BULK_REPLY_IOV:
		case CRT_OPC_SELF_TEST_BOTH_BULK:
			{
				struct crt_st_send_id_bulk *typed_args =
					(struct crt_st_send_id_bulk *)args;

				typed_args->bulk_hdl = cb_data->bulk_hdl;
				C_ASSERT(typed_args->bulk_hdl != CRT_BULK_NULL);
			}
			break;
		}

send_rpc:
		/* Give the callback a pointer to this endpoint entry */
		cb_data->endpt = endpt_ptr;

		ret = crt_gettime(&cb_data->sent_time);
		if (ret != 0) {
			C_ERROR("crt_gettime failed; ret = %d\n", ret);
			cb_args->fatal_err = ST_UNKNOWN;
			return ret;
		}

		/* Send the RPC */
		ret = crt_req_send(new_rpc, response_cb, cb_data);
		if (ret != 0) {
			C_WARN("crt_req_send failed for endpoint=%u:%u;"
			       " ret = %d\n",
			       local_endpt.ep_rank, local_endpt.ep_tag, ret);

			goto try_again;
		}

		/* RPC sent successfully */
		return 0;

try_again:
		/*
		 * Something must be wrong with this endpoint
		 * Mark it as evicted and try a different one instead
		 */
		C_WARN("Marking endpoint endpoint=%u:%u as evicted\n",
		       local_endpt.ep_rank, local_endpt.ep_tag);

		/*
		 * No need to lock cb_args->cb_args_lock here
		 *
		 * Lock or no lock the worst that can happen is that
		 * send_next_rpc() attempts to send another RPC to this endpoint
		 * and crt_req_send fails and the endpoint gets re-marked as
		 * evicted
		 */
		endpt_ptr->evicted = 1;
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
static int response_cb(const struct crt_cb_info *cb_info)
{
	struct st_cb_data	*cb_data = (struct st_cb_data *)
					   cb_info->cci_arg;
	struct st_cb_args	*cb_args = cb_data->cb_args;

	struct timespec		 now;

	/* Record latency of this RPC */
	crt_gettime(&now);
	cb_args->rep_latencies[cb_data->rep_idx].val =
		crt_timediff_ns(&cb_data->sent_time, &now);

	/* Record return code */
	cb_args->rep_latencies[cb_data->rep_idx].cci_rc = cb_info->cci_rc;

	/* If this endpoint was evicted during the RPC, mark it as so */
	if (cb_info->cci_rc == -CER_OOG) {
		/*
		 * No need to lock cb_args->cb_args_lock here
		 *
		 * Lock or no lock the worst that can happen is that
		 * send_next_rpc() attempts to send another RPC to this endpoint
		 * and crt_req_send fails and the endpoint gets re-marked as
		 * evicted
		 */
		cb_data->endpt->evicted = 1;
	}

	send_next_rpc(cb_data, 0);

	return 0;
}

static int open_session_cb(const struct crt_cb_info *cb_info)
{
	struct st_open_session_cb_data	*cb_data =
		(struct st_open_session_cb_data *)cb_info->cci_arg;
	struct st_cb_args		*cb_args = cb_data->cb_args;
	int32_t				*session_id;

	/* Get the session ID from the response message */
	session_id = (int32_t *)crt_reply_get(cb_info->cci_rpc);
	C_ASSERT(session_id != NULL);

	/* If this endpoint returned any kind of error, mark it is evicted */
	if (cb_info->cci_rc != 0) {
		C_WARN("Got cci_rc = %d while opening session with endpoint"
		       " %u:%u - removing it from the list of endpoints\n",
		       cb_info->cci_rc, cb_data->endpt->rank,
		       cb_data->endpt->tag);
		/* Nodes with evicted=1 are skipped for the rest of the test */
		cb_data->endpt->evicted = 1;
		cb_data->endpt->session_id = -1;
	} else if (*session_id < 0) {
		C_WARN("Got invalid session id = %d from endpoint %u:%u -\n"
		       " removing it from the list of endpoints\n",
		       *session_id, cb_data->endpt->rank, cb_data->endpt->tag);
		cb_data->endpt->evicted = 1;
		cb_data->endpt->session_id = -1;
	} else {
		/* Got a valid session_id - associate it with this endpoint */
		cb_data->endpt->session_id = *session_id;
	}

	/******************** LOCK: cb_args_lock ********************/
	pthread_spin_lock(&cb_args->cb_args_lock);

	cb_args->rep_completed_count++;

	pthread_spin_unlock(&cb_args->cb_args_lock);
	/******************* UNLOCK: cb_args_lock *******************/

	return 0;
}

static int close_session_cb(const struct crt_cb_info *cb_info)
{
	struct st_cb_args	*cb_args = (struct st_cb_args *)
					   cb_info->cci_arg;

	/******************** LOCK: cb_args_lock ********************/
	pthread_spin_lock(&cb_args->cb_args_lock);

	cb_args->rep_completed_count++;

	pthread_spin_unlock(&cb_args->cb_args_lock);
	/******************* UNLOCK: cb_args_lock *******************/

	return 0;
}

static int open_sessions(struct st_cb_args *cb_args, int max_inflight)
{
	struct st_open_session_cb_data	*cb_data_alloc = NULL;
	int32_t				 num_open_sent = 0;
	uint32_t			 i;
	int				 ret;

	/* Sessions are not required for (EMPTY EMPTY) */
	if (cb_args->test_params.send_type == CRT_SELF_TEST_MSG_TYPE_EMPTY &&
	    cb_args->test_params.reply_type == CRT_SELF_TEST_MSG_TYPE_EMPTY) {
		for (i = 0; i < cb_args->num_endpts; i++)
			cb_args->endpts[i].session_id = -1;
		return 0;
	}

	/* Allocate a buffer for arguments to the callback function */
	C_ALLOC(cb_data_alloc, cb_args->num_endpts *
			       sizeof(struct st_open_session_cb_data));
	if (cb_data_alloc == NULL)
		return -CER_NOMEM;

	/* Reset the completed counter that tracks how many opens succeeded */
	cb_args->rep_completed_count = 0;

	/*
	 * Dispatch an open to every specified endpoint
	 * If at any point sending to an endpoint fails, mark it as evicted
	 */
	for (i = 0; i < cb_args->num_endpts; i++) {
		crt_endpoint_t			 local_endpt;
		crt_rpc_t			*new_rpc;
		struct crt_st_session_params	*args;
		struct st_open_session_cb_data	*cb_data;

		local_endpt.ep_grp = cb_args->srv_grp;
		local_endpt.ep_rank = cb_args->endpts[i].rank;
		local_endpt.ep_tag = cb_args->endpts[i].tag;

		/* Start a new RPC request */
		ret = crt_req_create(cb_args->crt_ctx, local_endpt,
				     CRT_OPC_SELF_TEST_OPEN_SESSION,
				     &new_rpc);
		if (ret != 0) {
			C_WARN("crt_req_create failed for endpoint=%u:%u;"
			       " ret = %d\n",
			       local_endpt.ep_rank, local_endpt.ep_tag, ret);

			cb_args->endpts[i].session_id = -1;
			cb_args->endpts[i].evicted = 1;

			continue;
		}
		C_ASSERTF(new_rpc != NULL,
			  "crt_req_create succeeded but RPC is NULL\n");

		args = (struct crt_st_session_params *)crt_req_get(new_rpc);
		C_ASSERTF(args != NULL, "crt_req_get returned NULL\n");

		/* Copy lengths & types for send/reply */
		*args = cb_args->test_params;

		/*
		 * Set the number of buffers that the service should allocate.
		 * This is the maximum number of RPCs that the service should
		 * expect to see at any one time.
		 *
		 * TODO: Note this may have to change when randomizing endpoints
		 */
		args->num_buffers = max(1,
					min(max_inflight / cb_args->num_endpts,
					    (unsigned int)cb_args->rep_count));

		/* Set callback data */
		cb_data = &cb_data_alloc[i];
		cb_data->cb_args = cb_args;
		cb_data->endpt = &cb_args->endpts[i];

		/* Send the RPC */
		ret = crt_req_send(new_rpc, open_session_cb, cb_data);
		if (ret != 0) {
			C_WARN("crt_req_send failed for endpoint=%u:%u;"
			       " ret = %d\n",
			       local_endpt.ep_rank, local_endpt.ep_tag, ret);

			cb_args->endpts[i].session_id = -1;
			cb_args->endpts[i].evicted = 1;

			continue;
		}

		/* Successfully sent this open request - increment counter */
		num_open_sent++;
	}

	/* Wait until all opens complete */
	while (cb_args->rep_completed_count < num_open_sent)
		sched_yield();

	/* Free up the callback buffer */
	C_FREE(cb_data_alloc, cb_args->num_endpts * sizeof(struct st_cb_data));

	return 0;
}

static void close_sessions(struct st_cb_args *cb_args)
{
	int32_t		num_close_sent = 0;
	uint32_t	i;
	int		ret;

	/* Reset the completed counter that tracks how many closes succeeded */
	cb_args->rep_completed_count = 0;

	/*
	 * Dispatch a close to every specified endpoint
	 * If at any point sending to an endpoint fails, mark it as evicted
	 */
	for (i = 0; i < cb_args->num_endpts; i++) {
		crt_endpoint_t	 local_endpt;
		crt_rpc_t	*new_rpc;
		int32_t		*args;

		/* Don't bother to close sessions for nodes where open failed */
		if (cb_args->endpts[i].session_id < 0)
			continue;

		local_endpt.ep_grp = cb_args->srv_grp;
		local_endpt.ep_rank = cb_args->endpts[i].rank;
		local_endpt.ep_tag = cb_args->endpts[i].tag;

		/* Start a new RPC request */
		ret = crt_req_create(cb_args->crt_ctx, local_endpt,
				     CRT_OPC_SELF_TEST_CLOSE_SESSION,
				     &new_rpc);
		if (ret != 0) {
			C_WARN("Failed to close session %d on endpoint=%u:%u;"
			       " crt_req_created failed with ret = %d\n",
			       cb_args->endpts[i].session_id,
			       local_endpt.ep_rank, local_endpt.ep_tag, ret);

			/* Mark the node as evicted (likely already done) */
			cb_args->endpts[i].evicted = 1;

			continue;
		}
		C_ASSERTF(new_rpc != NULL,
			  "crt_req_create succeeded but RPC is NULL\n");

		args = (int32_t *)crt_req_get(new_rpc);
		C_ASSERTF(args != NULL, "crt_req_get returned NULL\n");

		*args = cb_args->endpts[i].session_id;

		/* Send the RPC */
		ret = crt_req_send(new_rpc, close_session_cb, cb_args);
		if (ret != 0) {
			C_WARN("crt_req_send failed for endpoint=%u:%u;"
			       " ret = %d\n",
			       local_endpt.ep_rank, local_endpt.ep_tag, ret);

			cb_args->endpts[i].session_id = -1;
			cb_args->endpts[i].evicted = 1;

			continue;
		}

		/* Successfully sent this close request - increment counter */
		num_close_sent++;
	}

	/* Wait until all closes complete */
	while (cb_args->rep_completed_count < num_close_sent)
		sched_yield();
}


static void *progress_fn(void *arg)
{
	int		 ret;
	crt_context_t	*crt_ctx = NULL;

	crt_ctx = (crt_context_t *)arg;
	C_ASSERT(crt_ctx != NULL);
	C_ASSERT(*crt_ctx != NULL);

	while (!g_shutdown_flag) {
		ret = crt_progress(*crt_ctx, 1, NULL, NULL);
		if (ret != 0 && ret != -CER_TIMEDOUT) {
			C_ERROR("crt_progress failed; ret = %d\n", ret);
			break;
		}
	};

	pthread_exit(NULL);
}

static int self_test_init(struct st_cb_args *cb_args, pthread_t *tid,
			  char *dest_name)
{
	char		my_group[] = "self_test";
	crt_rank_t	myrank;
	int		ret;

	ret = crt_init(my_group, 0);
	if (ret != 0) {
		C_ERROR("crt_init failed; ret = %d\n", ret);
		return ret;
	}

	ret = crt_group_attach(dest_name, &cb_args->srv_grp);
	if (ret != 0) {
		C_ERROR("crt_group_attach failed; ret = %d, srv_grp = %p\n",
			ret, cb_args->srv_grp);
		return ret;
	}
	C_DEBUG("attached to target: %s\n", cb_args->srv_grp->cg_grpid);
	C_ASSERTF(cb_args->srv_grp != NULL,
		  "crt_group_attach succeeded but returned group is NULL\n");

	ret = crt_context_create(NULL, &cb_args->crt_ctx);
	if (ret != 0) {
		C_ERROR("crt_context_create failed; ret = %d\n", ret);
		return ret;
	}

	ret = crt_group_rank(NULL, &myrank);
	if (ret != 0) {
		C_ERROR("crt_group_rank failed; ret = %d\n", ret);
		return ret;
	}

	g_shutdown_flag = 0;

	ret = pthread_create(tid, NULL, progress_fn, &cb_args->crt_ctx);
	if (ret != 0) {
		C_ERROR("failed to create progress thread: %s\n",
			strerror(errno));
		return -CER_MISC;
	}

	return 0;
}

static int st_compare_latencies(const void *a, const void *b)
{
	return ((struct st_latency *)a)->val > ((struct st_latency *)b)->val;
}

static int run_self_test(struct crt_st_session_params all_params[],
			 int num_msg_sizes, int rep_count, int max_inflight,
			 char *dest_name, struct st_endpoint *endpts,
			 uint32_t num_endpts)
{
	pthread_t			  tid;

	struct crt_st_session_params	  test_params = {0};
	int				  size_idx;
	int				  alloc_idx;
	int				  ret;
	int				  cleanup_ret;

	int64_t				  latency_avg;
	double				  latency_std_dev;
	double				  throughput;
	double				  bandwidth;
	struct timespec			  time_start_size, time_stop_size;

	/* Static arguments (same for each RPC callback function) */
	struct st_cb_args		  cb_args = {0};
	/* Private arguments data for all RPC callback functions */
	struct st_cb_data		**cb_data_ptrs = NULL;

	/* Set the callback data which will be the same for all callbacks */
	cb_args.rep_count = 1; /* First run only sends one message */
	pthread_spin_init(&cb_args.cb_args_lock, PTHREAD_PROCESS_PRIVATE);

	/* Initialize CART */
	ret = self_test_init(&cb_args, &tid, dest_name);
	if (ret != 0) {
		C_ERROR("self_test_init failed; ret = %d\n", ret);
		C_GOTO(cleanup_nothread, ret);
	}

	/* Allocate a buffer for latency measurements */
	C_ALLOC(cb_args.rep_latencies,
		rep_count * sizeof(cb_args.rep_latencies[0]));
	if (cb_args.rep_latencies == NULL)
		C_GOTO(cleanup, ret = -CER_NOMEM);

	/*
	 * Allocate an array of pointers to keep track of private
	 * per-inflight-rpc buffers
	 */
	C_ALLOC(cb_data_ptrs, max_inflight * sizeof(cb_data_ptrs[0]));
	if (cb_data_ptrs == NULL)
		C_GOTO(cleanup, ret = -CER_NOMEM);

	/* Refer to the endpoints passed by the caller */
	C_ASSERT(endpts != NULL && num_endpts > 0);
	cb_args.endpts = endpts;
	cb_args.num_endpts = num_endpts;

	/*
	 * Note this starts at -1, which is a special case for measuring
	 * the startup latency
	 */
	for (size_idx = -1; size_idx < num_msg_sizes; size_idx++) {
		int	 local_rep;
		int	 inflight_idx;
		int	 num_failed = 0;
		int	 num_passed = 0;
		size_t	 test_buf_len;

		if (size_idx == -1) {
			test_params.send_size = 0;
			test_params.reply_size = 0;
			test_params.send_type = CRT_SELF_TEST_MSG_TYPE_EMPTY;
			test_params.reply_type = CRT_SELF_TEST_MSG_TYPE_EMPTY;
		} else {
			test_params = all_params[size_idx];
			cb_args.rep_count = rep_count;
		}

		/*
		 * Compute the amount of spaced needed for this test run
		 * Note that if bulk is used for the reply, need to make sure
		 * this is big enough for the bulk reply to be written to
		 */
		if (ISBULK(test_params.reply_type))
			test_buf_len = max(test_params.send_size,
					   test_params.reply_size);
		else
			test_buf_len = test_params.send_size;

		/* Allocate "private" buffers for each inflight RPC */
		for (alloc_idx = 0; alloc_idx < max_inflight; alloc_idx++) {
			struct st_cb_data *cb_data;

			/* Only allocate nodes once and re-use them */
			if (cb_data_ptrs[alloc_idx] == NULL) {
				C_ALLOC_PTR(cb_data_ptrs[alloc_idx]);
				if (cb_data_ptrs[alloc_idx] == NULL) {
					C_ERROR("RPC data allocation failed\n");
					C_GOTO(cleanup, ret = -CER_NOMEM);
				}
			} else {
				memset(cb_data_ptrs[alloc_idx], 0,
				       sizeof(struct st_cb_data));
			}

			/*
			 * Now that this pointer can't change, get a shorter
			 * alias for it
			 */
			cb_data = cb_data_ptrs[alloc_idx];

			/*
			 * Free the buffer attached to this RPC instance if
			 * there is one from the previous size
			 */
			if (cb_data->buf != NULL)
				C_FREE(cb_data->buf, cb_data->buf_len);

			/* No buffer needed if there is no payload */
			if (test_buf_len == 0)
				continue;

			/* Allocate a new data buffer for this inflight RPC */
			C_ALLOC(cb_data->buf, test_buf_len);
			if (cb_data->buf == NULL) {
				C_ERROR("RPC data buf allocation failed\n");
				C_GOTO(cleanup, ret = -CER_NOMEM);
			}

			/* Fill the buffer with an arbitrary data pattern */
			memset(cb_data->buf, 0xC5, test_buf_len);

			/* Track how big the buffer is for bookkeeping */
			cb_data->buf_len = test_buf_len;

			/*
			 * Link the sg_list, iov's, and cb_data entries
			 *
			 * Note that here the length is the length of the actual
			 * buffer; this will probably need to be changed when it
			 * comes time to actually do a bulk transfer
			 */
			cb_data->sg_list.sg_iovs = &cb_data->sg_iov;
			cb_data->sg_list.sg_nr.num = 1;
			crt_iov_set(&cb_data->sg_iov, cb_data->buf,
				    test_buf_len);

			/* Create bulk handle if required */
			if (ISBULK(test_params.send_type) ||
			    ISBULK(test_params.reply_type)) {
				crt_bulk_perm_t perms =
					ISBULK(test_params.reply_type) ?
						CRT_BULK_RW : CRT_BULK_RO;

				ret = crt_bulk_create(cb_args.crt_ctx,
						      &cb_data->sg_list,
						      perms,
						      &cb_data->bulk_hdl);
				if (ret != 0)
					C_GOTO(cleanup, ret);
				C_ASSERT(cb_data->bulk_hdl != CRT_BULK_NULL);
			}
		}

		/* Set remaining callback data argument that changes per test */
		cb_args.test_params = test_params;

		/* Open self-test sessions with every endpoint */
		ret = open_sessions(&cb_args, max_inflight);
		if (ret != 0)
			C_GOTO(cleanup, ret = -CER_NOMEM);

		/* Initialize the latencies to -1 to indicate invalid data */
		for (local_rep = 0; local_rep < cb_args.rep_count; local_rep++)
			cb_args.rep_latencies[local_rep].val = -1;

		/* Record the time right when we start processing this size */
		ret = crt_gettime(&time_start_size);
		if (ret != 0) {
			C_ERROR("crt_gettime failed; ret = %d\n", ret);
			C_GOTO(cleanup, ret);
		}

		/* Restart the RPCs completed counters */
		cb_args.rep_completed_count = 0;
		cb_args.rep_idx = 0;
		cb_args.next_endpt_idx = 0;
		cb_args.fatal_err = ST_SUCCESS;

		for (inflight_idx = 0; inflight_idx < max_inflight;
		     inflight_idx++) {
			struct st_cb_data *cb_data = cb_data_ptrs[inflight_idx];

			cb_data->cb_args = &cb_args;
			cb_data->rep_idx = -1;

			ret = send_next_rpc(cb_data, 1);
			if (ret != 0) {
				C_ERROR("All endpoints marked as evicted while"
					" generating initial inflight RPCs\n");
				C_GOTO(cleanup, ret);
			}
		}

		/* Wait until all the RPCs come back */
		while (cb_args.rep_completed_count < cb_args.rep_count
		       || cb_args.fatal_err != ST_SUCCESS)
			sched_yield();

		if (cb_args.fatal_err == ST_UNREACH) {
			C_ERROR("All endpoints marked as evicted during"
				" self-test run\n");
			ret = cb_args.fatal_err;
			C_GOTO(cleanup, ret);
		} else if (cb_args.fatal_err != ST_SUCCESS) {
			C_ERROR("Got fatal error %d while processing RPCs\n",
				cb_args.fatal_err);
			ret = cb_args.fatal_err;
			C_GOTO(cleanup, ret);
		}

		/* Record the time right when we stopped processing this size */
		ret = crt_gettime(&time_stop_size);
		if (ret != 0) {
			C_ERROR("crt_gettime failed; ret = %d\n", ret);
			C_GOTO(cleanup, ret);
		}

		/* Close outstanding self-test sessions with every endpoint */
		close_sessions(&cb_args);

		/* Free the bulk handles if they were used */
		for (alloc_idx = 0; alloc_idx < max_inflight; alloc_idx++) {
			struct st_cb_data *cb_data = cb_data_ptrs[alloc_idx];

			C_ASSERT(cb_data != NULL);

			if (cb_data->bulk_hdl != CRT_BULK_NULL) {
				crt_bulk_free(cb_data->bulk_hdl);
				cb_data->bulk_hdl = NULL;
			}
		}

		/* Print out the first message latency separately */
		if (size_idx == -1 && cb_args.rep_latencies[0].val < 0) {
			printf("\tFirst RPC (size=(0 0)) failed; ret = %ld\n",
			       cb_args.rep_latencies[0].val);
			continue;
		} else if (size_idx == -1) {
			printf("First RPC latency (size=(0 0)) (us): %ld\n\n",
			       cb_args.rep_latencies[0].val / 1000);
			continue;
		}

		/* Compute the throughput and bandwidth for this size */
		throughput = cb_args.rep_count /
			(crt_timediff_ns(&time_start_size, &time_stop_size) /
			 1000000000.0F);
		bandwidth = throughput * (test_params.send_size +
					  test_params.reply_size);

		/* Print the results for this size */
		printf("Results for message size (%d-%s %d-%s)"
		       " (max_inflight_rpcs = %d)\n",
		       test_params.send_size,
		       crt_st_msg_type_str[test_params.send_type],
		       test_params.reply_size,
		       crt_st_msg_type_str[test_params.reply_type],
		       max_inflight);
		printf("\tRPC Bandwidth (MB/sec): %.2f\n",
		       bandwidth / 1000000.0F);
		printf("\tRPC Throughput (RPCs/sec): %.0f\n", throughput);

		/*
		 * TODO:
		 * In the future, probably want to return the latencies here
		 * before they are sorted. This will also provide the ability
		 * to do some analytics on the RPCs that failed before their
		 * latencies are overwritten
		 */

		/* Figure out how many repetitions were errors */
		num_failed = 0;
		for (local_rep = 0; local_rep < cb_args.rep_count; local_rep++)
			if (cb_args.rep_latencies[local_rep].cci_rc < 0) {
				num_failed++;

				/* Since this RPC failed, overwrite its latency
				 * with -1 so it will sort before any passing
				 * RPCs. This segments the latencies into two
				 * sections - from [0:num_failed] will be -1,
				 * and from [num_failed:] will be succesful RPC
				 * latencies
				 */
				cb_args.rep_latencies[local_rep].val = -1;

			}

		/*
		 * Compute number successful and exit early if none worked to
		 * guard against overflow and divide by zero later
		 */
		num_passed = cb_args.rep_count - num_failed;
		if (num_passed == 0) {
			printf("\tAll RPCs for this message size failed\n");
			continue;
		}

		/* Sort the latencies */
		qsort(cb_args.rep_latencies, cb_args.rep_count,
		      sizeof(cb_args.rep_latencies[0]), st_compare_latencies);

		/* Compute average and standard deviation */
		latency_avg = 0;
		for (local_rep = num_failed; local_rep < cb_args.rep_count;
		     local_rep++)
			latency_avg += cb_args.rep_latencies[local_rep].val;
		latency_avg /= cb_args.rep_count;

		latency_std_dev = 0;
		for (local_rep = num_failed; local_rep < cb_args.rep_count;
		     local_rep++)
			latency_std_dev +=
				pow(cb_args.rep_latencies[local_rep].val -
				    latency_avg,
				    2);
		latency_std_dev /= num_passed;
		latency_std_dev = sqrt(latency_std_dev);

		/* Print Latency Results */
		printf("\tRPC Failures: %d\n"
		       "\tRPC Latencies (us):\n"
		       "\t\tMin    : %ld\n"
		       "\t\t25th  %%: %ld\n"
		       "\t\tMedian : %ld\n"
		       "\t\t75th  %%: %ld\n"
		       "\t\tMax    : %ld\n"
		       "\t\tAverage: %ld\n"
		       "\t\tStd Dev: %.2f\n",
		       num_failed,
		       cb_args.rep_latencies[num_failed].val / 1000,
		       cb_args.rep_latencies[num_failed + num_passed / 4].val
				/ 1000,
		       cb_args.rep_latencies[num_failed + num_passed / 2].val
				/ 1000,
		       cb_args.rep_latencies[num_failed + num_passed*3/4].val
				/ 1000,
		       cb_args.rep_latencies[cb_args.rep_count - 1].val / 1000,
		       latency_avg / 1000, latency_std_dev / 1000);
		printf("\n");
	}

cleanup:
	/* Tell the progress thread to abort and exit */
	g_shutdown_flag = 1;

	ret = pthread_join(tid, NULL);
	if (ret)
		C_ERROR("Could not join progress thread");

cleanup_nothread:
	if (cb_args.rep_latencies != NULL)
		C_FREE(cb_args.rep_latencies,
		       rep_count * sizeof(cb_args.rep_latencies[0]));
	if (cb_data_ptrs != NULL) {
		for (alloc_idx = 0; alloc_idx < max_inflight; alloc_idx++) {
			struct st_cb_data *cb_data = cb_data_ptrs[alloc_idx];

			if (cb_data != NULL) {
				if (cb_data->bulk_hdl != CRT_BULK_NULL) {
					crt_bulk_free(cb_data->bulk_hdl);
					cb_data->bulk_hdl = NULL;
				}
				if (cb_data->buf != NULL)
					C_FREE(cb_data->buf,
					      cb_data->buf_len);

				/*
				 * Free and zero the actual pointer, not
				 * the local copy
				 */
				C_FREE_PTR(cb_data_ptrs[alloc_idx]);
			}
		}

		C_FREE(cb_data_ptrs, max_inflight * sizeof(cb_data_ptrs[0]));
	}

	if (cb_args.srv_grp != NULL) {
		cleanup_ret = crt_group_detach(cb_args.srv_grp);
		if (cleanup_ret != 0)
			C_ERROR("crt_group_detach failed; ret = %d\n",
				cleanup_ret);
		/* Make sure first error is returned, if applicable */
		ret = ((ret == 0) ? cleanup_ret : ret);
	}

	cleanup_ret = crt_context_destroy(cb_args.crt_ctx, 0);
	if (cleanup_ret != 0)
		C_ERROR("crt_context_destroy failed; ret = %d\n", cleanup_ret);
	/* Make sure first error is returned, if applicable */
	ret = ((ret == 0) ? cleanup_ret : ret);

	cleanup_ret = crt_finalize();
	if (cleanup_ret != 0)
		C_ERROR("crt_finalize failed; ret = %d\n", cleanup_ret);
	/* Make sure first error is returned, if applicable */
	ret = ((ret == 0) ? cleanup_ret : ret);

	return ret;
}

static void print_usage(const char *prog_name, const char *msg_sizes_str,
			int rep_count,
			int max_inflight)
{
	/* TODO --randomize-endpoints */
	/* TODO --verbose */
	printf("Usage: %s --group-name <name> --endpoint <ranks:tags> [optional arguments]\n"
	       "\n"
	       "Required Arguments\n"
	       "  --group-name <group_name>\n"
	       "      Short version: -g\n"
	       "      The name of the process set to test against\n"
	       "  --endpoint <name:ranks:tags>\n"
	       "      Short version: -e\n"
	       "      Describes an endpoint (or range of endpoints) to connect to\n"
	       "        Note: Can be specified multiple times\n"
	       "\n"
	       "      ranks and tags are comma-separated lists to connect to\n"
	       "        Supports both ranges and lists - for example, \"1-5,3,8\"\n"
	       "\n"
	       "      Example: --endpoint 1-3,2:0-1\n"
	       "        This would create these endpoints:\n"
	       "          1:0\n"
	       "          1:1\n"
	       "          2:0\n"
	       "          2:1\n"
	       "          3:0\n"
	       "          3:1\n"
	       "          2:0\n"
	       "          2:1\n"
	       "\n"
	       "        By default, self-test will send test messages to these\n"
	       "        endpoints in the order listed above. See --randomize-endpoints\n"
	       "        for more information\n"
	       "\n"
	       "Optional Arguments\n"
	       "  --message-sizes <(a b),(c d),...>\n"
	       "      Short version: -s\n"
	       "      List of size tuples (in bytes) to use for the self test.\n"
	       "\n"
	       "      Note that the ( ) are not strictly necessary\n"
	       "      Providing a single size (a) is interpreted as an alias for (a a)\n"
	       "\n"
	       "      For each tuple, the first value is the sent size, and the second value is the reply size\n"
	       "      Valid sizes are [0-%d]\n"
	       "      Performance results will be reported individually for each tuple.\n"
	       "\n"
	       "      Each size integer can be prepended with a single character to specify\n"
	       "      the underlying transport mechanism. Available types are:\n"
	       "        'e' - Empty (no payload)\n"
	       "        'i' - I/O vector (IOV)\n"
	       "        'b' - Bulk transfer\n"
	       "      For example, (b1000) would transfer 1000 bytes via bulk in both directions\n"
	       "      Similarly, (i100 b1000) would use IOV to send and bulk to reply\n"
	       "      Only reasonable combinations are permitted (i.e. e1000 is not allowed)\n"
	       "      If no type specifier is specified, one will be chosen automatically. The simple\n"
	       "        heuristic is that bulk will be used if a specified size is >= %u\n"
	       "      BULK_GET will be used on the service side to 'send' data from client\n"
	       "        to service, and BULK_PUT will be used on the service side to 'reply'\n"
	       "        (assuming bulk transfers specified)\n"
	       "\n"
	       "      Note that different messages are sent internally via different structures.\n"
	       "      These are enumerated as follows, with x,y > 0:\n"
	       "        (0  0)  - Empty payload sent in both directions\n"
	       "        (ix 0)  - 4-byte session_id + x-byte iov sent, empty reply\n"
	       "        (0  iy) - 4-byte session_id sent, y-byte iov reply\n"
	       "        (ix iy) - 4-byte session_id + x-byte iov sent, y-byte iov reply\n"
	       "        (0  by) - 4-byte session_id + 8-byte bulk handle sent\n"
	       "                  y-byte BULK_PUT, empty reply\n"
	       "        (bx 0)  - 4-byte session_id + 8-byte bulk_handle sent\n"
	       "                  x-byte BULK_GET, empty reply\n"
	       "        (ix by) - 4-byte session_id + x-byte iov + 8-byte bulk_handle sent\n"
	       "                  y-byte BULK_PUT, empty reply\n"
	       "        (bx iy) - 4-byte session_id + 8-byte bulk_handle sent\n"
	       "                  x-byte BULK_GET, y-byte iov reply\n"
	       "        (bx by) - 4-byte session_id + 8-byte bulk_handle sent\n"
	       "                  x-byte BULK_GET, y-byte BULK_PUT, empty reply\n"
	       "\n"
	       "      Note also that any message size other than (0 0) will use test sessions.\n"
	       "        A self-test session will be negotiated with the service before sending\n"
	       "        any traffic, and the session will be closed after testing this size completes.\n"
	       "        The time to create and tear down these sessions is NOT measured.\n"
	       "\n"
	       "      Default: \"%s\"\n"
	       "\n"
	       "  --repetitions-per-size <N>\n"
	       "      Short version: -r\n"
	       "      Number of samples per message size. RPCs for each particular size\n"
	       "      will be repeated this many times.\n"
	       "      Default: %d\n"
	       "\n"
	       "  --max-inflight-rpcs <N>\n"
	       "      Short version: -i\n"
	       "      Maximum number of RPCs allowed to be executing concurrently.\n"
	       "\n"
	       "      Note that at the beginning of each test run, a buffer of size send_size\n"
	       "        is allocated for each inflight RPC (total max_inflight * send_size).\n"
	       "        This could be a lot of memory. Also, if the reply uses bulk, the\n"
	       "        size increases to (max_inflight * max(send_size, reply_size))\n"
	       "\n"
	       "      Default: %d\n",
	       prog_name, CRT_SELF_TEST_MAX_MSG_SIZE,
	       CRT_SELF_TEST_AUTO_BULK_THRESH, msg_sizes_str, rep_count,
	       max_inflight);
}

#define ST_ENDPT_RANK_IDX 0
#define ST_ENDPT_TAG_IDX 1
static int st_validate_range_str(const char *str)
{
	const char *start = str;

	while (*str != '\0') {
		if ((*str < '0' || *str > '9')
		    && (*str != '-') && (*str != ',')) {
			return -CER_INVAL;
		}

		str++;

		/* Make sure the range string isn't ridiculously large */
		if (str - start > SELF_TEST_MAX_LIST_STR_LEN)
			return -CER_INVAL;
	}
	return 0;
}

static void st_parse_range_str(char *const str, char *const validated_str,
			       uint32_t *num_elements)
{
	char *pch;
	char *pch_sub;
	char *saveptr_comma = NULL;
	char *saveptr_hyphen = NULL;
	char *validated_cur_ptr = validated_str;

	/* Split into tokens based on commas */
	pch = strtok_r(str, ",", &saveptr_comma);
	while (pch != NULL) {
		/* Number of valid hyphen-delimited values encountered so far */
		int		hyphen_count = 0;
		/* Start/stop values */
		uint32_t	val[2] = {0, 0};
		/* Flag indicating if values were filled, 0=no 1=yes */
		int		val_valid[2] = {0, 0};

		/* Number of characters left to write to before overflowing */
		int		num_avail = SELF_TEST_MAX_LIST_STR_LEN -
					(validated_cur_ptr - validated_str);
		int		num_written = 0;

		/*
		 * Split again on hyphens, using only the first two non-empty
		 * values
		 */
		pch_sub = strtok_r(pch, "-", &saveptr_hyphen);
		while (pch_sub != NULL && hyphen_count < 2) {
			if (*pch_sub != '\0') {
				/*
				 * Seems like we have a valid number.
				 * If anything goes wrong, skip over this
				 * comma-separated range/value.
				 */
				if (sscanf(pch_sub, "%u", &val[hyphen_count])
				    != 1) {
					val_valid[0] = 0;
					val_valid[1] = 0;
					break;
				}

				val_valid[hyphen_count] = 1;

				hyphen_count++;
			}

			pch_sub = strtok_r(NULL, "-", &saveptr_hyphen);
		}

		if (val_valid[0] == 1 && val_valid[1] == 1) {
			/* This was a valid range */
			uint32_t min = val[0] < val[1] ? val[0] : val[1];
			uint32_t max = val[0] > val[1] ? val[0] : val[1];

			*num_elements += max - min + 1;
			num_written = snprintf(validated_cur_ptr, num_avail,
					       "%u-%u,", min, max);
		} else if (val_valid[0] == 1) {
			/* Only one valid value */
			*num_elements += 1;
			num_written = snprintf(validated_cur_ptr, num_avail,
					       "%u,", val[0]);
		}

		/*
		 * It should not be possible to provide input that gets larger
		 * after sanition
		 */
		C_ASSERT(num_written <= num_avail);

		validated_cur_ptr += num_written;

		pch = strtok_r(NULL, ",", &saveptr_comma);
	}

	/* Trim off the trailing , */
	if (validated_cur_ptr > validated_str)
		*(validated_cur_ptr - 1) = '\0';
}

int parse_endpoint_string(char *const optarg, struct st_endpoint **const endpts,
			  uint32_t *const num_endpts)
{
	char			*token_ptrs[2] = {NULL, NULL};
	int			 separator_count = 0;
	int			 ret = 0;
	char			*pch = NULL;
	char			*rank_valid_str = NULL;
	uint32_t		 num_ranks = 0;
	char			*tag_valid_str = NULL;
	uint32_t		 num_tags = 0;
	void			*realloced_mem;
	struct st_endpoint	*next_endpoint;

	/*
	 * strtok replaces separators with \0 characters
	 * Use this to divide up the input argument into three strings
	 *
	 * Use the first three ; delimited strings - ignore the rest
	 */
	pch = strtok(optarg, ":");
	while (pch != NULL && separator_count < 2) {
		token_ptrs[separator_count] = pch;

		separator_count++;

		pch = strtok(NULL, ":");
	}

	/* Validate the input strings */
	if (token_ptrs[ST_ENDPT_RANK_IDX] == NULL
	    || token_ptrs[ST_ENDPT_TAG_IDX] == NULL
	    || *token_ptrs[ST_ENDPT_RANK_IDX] == '\0'
	    || *token_ptrs[ST_ENDPT_TAG_IDX] == '\0') {
		printf("endpoint must contain non-empty rank:tag\n");
		return -CER_INVAL;
	}
	/* Both group and tag can only contain [0-9\-,] */
	if (st_validate_range_str(token_ptrs[ST_ENDPT_RANK_IDX]) != 0) {
		printf("endpoint rank contains invalid characters\n");
		return -CER_INVAL;
	}
	if (st_validate_range_str(token_ptrs[ST_ENDPT_TAG_IDX]) != 0) {
		printf("endpoint tag contains invalid characters\n");
		return -CER_INVAL;
	}

	/*
	 * Now that strings have been sanity checked, allocate some space for a
	 * fully-validated copy of the rank and tag list. This works as follows:
	 * - The input string is tokenized and parsed
	 * - Each value (or range) is checked for validity
	 * - If valid, that range is written to the _valid_ string and the
	 *   number of elements in that range is added to the counter
	 *
	 * After both ranks and tags have been validated, the endpoint array can
	 * be resized to accomodate the new entries and the validated string
	 * can be re-parsed (without error checking) to add elements to the
	 * array.
	 */
	C_ALLOC(rank_valid_str, SELF_TEST_MAX_LIST_STR_LEN);
	if (rank_valid_str == NULL)
		C_GOTO(cleanup, ret = -CER_NOMEM);

	C_ALLOC(tag_valid_str, SELF_TEST_MAX_LIST_STR_LEN);
	if (tag_valid_str == NULL)
		C_GOTO(cleanup, ret = -CER_NOMEM);

	st_parse_range_str(token_ptrs[ST_ENDPT_RANK_IDX], rank_valid_str,
			   &num_ranks);

	st_parse_range_str(token_ptrs[ST_ENDPT_TAG_IDX], tag_valid_str,
			   &num_tags);

	/* Validate num_ranks and num_tags */
	if ((((uint64_t)num_ranks * (uint64_t)num_tags)
	     > (uint64_t)SELF_TEST_MAX_NUM_ENDPOINTS) ||
	    (((uint64_t)*num_endpts + (uint64_t)num_ranks * (uint64_t)num_tags)
	     > (uint64_t)SELF_TEST_MAX_NUM_ENDPOINTS)) {
		C_ERROR("Too many endpoints - current=%u, "
			"additional requested=%lu, max=%u\n",
			*num_endpts,
			(uint64_t)num_ranks * (uint64_t)num_tags,
			SELF_TEST_MAX_NUM_ENDPOINTS);
		C_GOTO(cleanup, ret = -CER_INVAL);
	}

	printf("Adding endpoints:\n");
	printf("  ranks: %s (# ranks = %u)\n", rank_valid_str, num_ranks);
	printf("  tags: %s (# tags = %u)\n", tag_valid_str, num_tags);

	/* Reallocate/expand the endpoints array */
	*num_endpts += num_ranks * num_tags;
	realloced_mem = C_REALLOC(*endpts,
				  sizeof(struct st_endpoint) * (*num_endpts));
	if (realloced_mem == NULL)
		C_GOTO(cleanup, ret = -CER_NOMEM);
	*endpts = (struct st_endpoint *)realloced_mem;

	/* Populate the newly expanded values in the endpoints array */
	next_endpoint = *endpts + *num_endpts - (num_ranks * num_tags);

	/*
	 * This block uses simpler tokenization logic (not strtok) because it
	 * has already been pre-validated
	 */

	/* Iterate over all rank tokens */
	pch = rank_valid_str;
	while (*pch != '\0') {
		uint32_t	rank;
		uint32_t	rank_max;
		int		num_scanned;

		num_scanned = sscanf(pch, "%u-%u", &rank, &rank_max);
		C_ASSERT(num_scanned == 1 || num_scanned == 2);
		if (num_scanned == 1)
			rank_max = rank;

		/* For this rank token, iterate over its range */
		do {
			uint32_t	 tag;
			uint32_t	 tag_max;
			char		*pch_tag;

			pch_tag = tag_valid_str;

			/*
			 * For this particular rank, iterate over all tag tokens
			 */
			while (*pch_tag != '\0') {
				num_scanned = sscanf(pch_tag, "%u-%u", &tag,
						     &tag_max);
				C_ASSERT(num_scanned == 1 || num_scanned == 2);
				if (num_scanned == 1)
					tag_max = tag;

				/*
				 * For this rank and chosen tag token, iterate
				 * over the range of tags in this tag token
				 */
				do {
					next_endpoint->rank = rank;
					next_endpoint->tag = tag;
					next_endpoint->evicted = 0;
					next_endpoint++;
					tag++;
				} while (tag <= tag_max);

				/* Advance the pointer just past the next , */
				do {
					pch_tag++;
				} while (*pch_tag != '\0' && *pch_tag != ',');
				if (*pch_tag == ',')
					pch_tag++;
			}

			rank++;
		} while (rank <= rank_max);

		/* Advance the pointer just past the next , */
		do {
			pch++;
		} while (*pch != '\0' && *pch != ',');
		if (*pch == ',')
			pch++;
	}

	/* Make sure all the allocated space got filled with real endpoints */
	C_ASSERT(next_endpoint == *endpts + *num_endpts);

	ret = 0;

cleanup:
	if (rank_valid_str != NULL)
		C_FREE(rank_valid_str, SELF_TEST_MAX_LIST_STR_LEN);
	if (tag_valid_str != NULL)
		C_FREE(tag_valid_str, SELF_TEST_MAX_LIST_STR_LEN);

	return ret;

}

/**
 * Parse a message size tuple from the user. The input format for this is
 * described in the usage text - basically one or two unsigned integer sizes,
 * each optionally prefixed by a character that specifies what underlying IO
 * type should be used to transfer a payload of that size (empty, iov, bulk).
 *
 * \return	0 on successfully filling *test_params, nonzero otherwise
 */
int parse_message_sizes_string(const char *pch,
			       struct crt_st_session_params *test_params)
{
	/*
	 * Note whether a type is specified or not. If no type is
	 * specified by the user, it will be automatically selected
	 */
	int send_type_specified = 0;
	int reply_type_specified = 0;

	/*
	 * A simple map between identifier ('e') and type (...EMPTY)
	 *
	 * Note that BULK_PUT (for send) or BULK_GET (for reply) are
	 * not yet implemented. For this reason, only 'b' for bulk is
	 * accepted as a type here, which will automatically choose
	 * PUT or GET depending on the direction.
	 *
	 * If send/PUT or reply/GET are ever implemented, the map can be
	 * easily changed to support this.
	 */
	const struct {
		char identifier;
		enum crt_st_msg_type type;
	} type_map[] = { {'e', CRT_SELF_TEST_MSG_TYPE_EMPTY},
			 {'i', CRT_SELF_TEST_MSG_TYPE_IOV},
			 {'b', CRT_SELF_TEST_MSG_TYPE_BULK_GET} };

	/* Number of types recognized */
	const int num_types = ARRAY_SIZE(type_map);

	int ret;

	/*
	 * Advance pch to the next numerical character in the token
	 * If along the way pch happens to be one of the type
	 * characters, note down that type and continue hunting for a
	 * number. In this way, only the last type specifier before the
	 * number is stored.
	 */
	while (*pch != '\0' && (*pch < '0' || *pch > '9')) {
		int i;

		for (i = 0; i < num_types; i++)
			if (*pch == type_map[i].identifier) {
				send_type_specified = 1;
				test_params->send_type = type_map[i].type;
			}
		pch++;
	}
	if (*pch == '\0')
		return -1;

	/* Read the first size */
	ret = sscanf(pch, "%u", &test_params->send_size);
	if (ret != 1 || (test_params->send_size > CRT_SELF_TEST_MAX_MSG_SIZE))
		return -1;

	/* Advance pch to the next non-numeric character */
	while (*pch != '\0' && *pch >= '0' && *pch <= '9')
		pch++;

	/*
	 * Advance pch to the next numerical character in the token
	 * If along the way pch happens to be one of the type
	 * characters, note down that type and continue hunting for a
	 * number. In this way, only the last type specifier before the
	 * number is stored.
	 */
	while (*pch != '\0' && (*pch < '0' || *pch > '9')) {
		int i;

		for (i = 0; i < num_types; i++)
			if (*pch == type_map[i].identifier) {
				reply_type_specified = 1;
				test_params->reply_type = type_map[i].type;
			}
		pch++;
	}
	if (*pch != '\0') {
		/* Read the second size */
		ret = sscanf(pch, "%u", &test_params->reply_size);
		if (ret != 1 ||
		    (test_params->reply_size > CRT_SELF_TEST_MAX_MSG_SIZE))
			return -1;
	} else {
		/* Only one numerical value - that's perfectly valid */
		test_params->reply_size = test_params->send_size;
		test_params->reply_type = test_params->send_type;
		reply_type_specified = send_type_specified;
	}

	/* If we got here, the send_size and reply_size are valid */

	/***** Automatically assign types if they were not specified *****/
	if (send_type_specified == 0) {
		if (test_params->send_size == 0)
			test_params->send_type = CRT_SELF_TEST_MSG_TYPE_EMPTY;
		else if (test_params->send_size <
			  CRT_SELF_TEST_AUTO_BULK_THRESH)
			test_params->send_type = CRT_SELF_TEST_MSG_TYPE_IOV;
		else
			test_params->send_type =
				CRT_SELF_TEST_MSG_TYPE_BULK_GET;
	}
	if (reply_type_specified == 0) {
		if (test_params->reply_size == 0)
			test_params->reply_type = CRT_SELF_TEST_MSG_TYPE_EMPTY;
		else if (test_params->reply_size <
			  CRT_SELF_TEST_AUTO_BULK_THRESH)
			test_params->reply_type = CRT_SELF_TEST_MSG_TYPE_IOV;
		else
			test_params->reply_type =
				CRT_SELF_TEST_MSG_TYPE_BULK_PUT;
	}

	/***** Silently / automatically correct invalid types *****/
	/* Empty messages always have empty type */
	if (test_params->send_size == 0)
		test_params->send_type = CRT_SELF_TEST_MSG_TYPE_EMPTY;
	if (test_params->reply_size == 0)
		test_params->reply_type = CRT_SELF_TEST_MSG_TYPE_EMPTY;

	/* All other empty requests with nonzero payload convert to iov */
	if (test_params->send_size != 0 &&
	    test_params->send_type == CRT_SELF_TEST_MSG_TYPE_EMPTY)
		test_params->send_type = CRT_SELF_TEST_MSG_TYPE_IOV;
	if (test_params->reply_size != 0 &&
	    test_params->reply_type == CRT_SELF_TEST_MSG_TYPE_EMPTY)
		test_params->reply_type = CRT_SELF_TEST_MSG_TYPE_IOV;

	/* Bulk requests convert to the type allowed by send/reply */
	if (test_params->send_type == CRT_SELF_TEST_MSG_TYPE_BULK_PUT)
		test_params->send_type = CRT_SELF_TEST_MSG_TYPE_BULK_GET;
	if (test_params->reply_type == CRT_SELF_TEST_MSG_TYPE_BULK_GET)
		test_params->reply_type = CRT_SELF_TEST_MSG_TYPE_BULK_PUT;

	return 0;
}

int main(int argc, char *argv[])
{
	/* Default parameters */
	char				 default_msg_sizes_str[] =
		 "b200000,b200000 0,0 b200000,b200000 i1000,i1000 b200000,"
		 "i1000,i1000 0,0 i1000,0";
	const int			 default_rep_count = 10000;
	const int			 default_max_inflight = 1000;

	char				*dest_name = NULL;
	const char			 tuple_tokens[] = "(),";
	char				*msg_sizes_str = default_msg_sizes_str;
	int				 rep_count = default_rep_count;
	int				 max_inflight = default_max_inflight;
	struct crt_st_session_params	*all_params = NULL;
	char				*sizes_ptr = NULL;
	char				*pch = NULL;
	int				 num_msg_sizes;
	int				 num_tokens;
	int				 c;
	int				 j;
	int				 ret = 0;
	struct st_endpoint		*endpts = NULL;
	uint32_t			 num_endpts = 0;

	/********************* Parse user arguments *********************/
	while (1) {
		static struct option long_options[] = {
			{"group-name", required_argument, 0, 'g'},
			{"endpoint", required_argument, 0, 'e'},
			{"message-sizes", required_argument, 0, 's'},
			{"repetitions-per-size", required_argument, 0, 'r'},
			{"max-inflight-rpcs", required_argument, 0, 'i'},
			{0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "g:e:s:r:i:", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'g':
			dest_name = optarg;
			break;
		case 'e':
			parse_endpoint_string(optarg, &endpts, &num_endpts);
			break;
		case 's':
			msg_sizes_str = optarg;
			break;
		case 'r':
			ret = sscanf(optarg, "%d", &rep_count);
			if (ret != 1) {
				rep_count = default_rep_count;
				printf("Warning: Invalid repetitions-per-size\n"
				       "  Using default value %d instead\n",
				       rep_count);
			}
			break;
		case 'i':
			ret = sscanf(optarg, "%d", &max_inflight);
			if (ret != 1) {
				max_inflight = default_max_inflight;
				printf("Warning: Invalid max-inflight-rpcs\n"
				       "  Using default value %d instead\n",
				       max_inflight);
			}
			break;
		case '?':
		default:
			print_usage(argv[0], default_msg_sizes_str,
				    default_rep_count,
				    default_max_inflight);
			C_GOTO(cleanup, ret = -CER_INVAL);
		}
	}

	/******************** Parse message sizes argument ********************/

	/*
	 * Count the number of tuple tokens (',') in the user-specified string
	 * This gives an upper limit on the number of arguments the user passed
	 */
	num_tokens = 0;
	sizes_ptr = msg_sizes_str;
	while (1) {
		const char *token_ptr = tuple_tokens;

		/* Break upon reaching the end of the argument */
		if (*sizes_ptr == '\0')
			break;

		/*
		 * For each valid token, check if this character is that token
		 */
		while (1) {
			if (*token_ptr == '\0')
				break;
			if (*token_ptr == *sizes_ptr)
				num_tokens++;
			token_ptr++;
		}

		sizes_ptr++;
	}

	/* Allocate a large enough buffer to hold the message sizes list */
	C_ALLOC(all_params, (num_tokens + 1) * sizeof(all_params[0]));
	if (all_params == NULL)
		C_GOTO(cleanup, ret = -CER_NOMEM);

	/* Iterate over the user's message sizes and parse / validate them */
	num_msg_sizes = 0;
	pch = strtok(msg_sizes_str, tuple_tokens);
	while (pch != NULL) {
		C_ASSERTF(num_msg_sizes <= num_tokens, "Token counting err\n");

		ret = parse_message_sizes_string(pch,
						 &all_params[num_msg_sizes]);
		if (ret == 0)
			num_msg_sizes++;
		else
			printf("Warning: Invalid message sizes tuple\n"
			       "  Expected values in range [0:%u], got '%s'\n",
			       CRT_SELF_TEST_MAX_MSG_SIZE,
			       pch);

		pch = strtok(NULL, tuple_tokens);
	}

	if (num_msg_sizes <= 0) {
		printf("No valid message sizes given\n");
		C_GOTO(cleanup, ret = -CER_INVAL);
	}

	/* Shrink the buffer if some of the user's tokens weren't kept */
	if (num_msg_sizes < num_tokens + 1) {
		void *realloced_mem;

		/* This should always succeed since the buffer is shrinking.. */
		realloced_mem = C_REALLOC(all_params,
					  num_msg_sizes
					  * sizeof(all_params[0]));
		if (realloced_mem == NULL)
			C_GOTO(cleanup, ret = -CER_NOMEM);
		all_params = (struct crt_st_session_params *)realloced_mem;
	}

	/******************** Validate arguments ********************/
	if (dest_name == NULL || crt_validate_grpid(dest_name) != 0) {
		printf("--group-name argument not specified or is invalid\n");
		C_GOTO(cleanup, ret = -CER_INVAL);
	}
	if (endpts == NULL || num_endpts == 0) {
		printf("No endpoints specified\n");
		C_GOTO(cleanup, ret = -CER_INVAL);
	}
	if ((rep_count <= 0) || (rep_count > SELF_TEST_MAX_REPETITIONS)) {
		printf("Invalid --repetitions-per-size argument\n"
		       "  Expected value in range (0:%d], got %d\n",
		       SELF_TEST_MAX_REPETITIONS, rep_count);
		C_GOTO(cleanup, ret = -CER_INVAL);
	}
	if ((max_inflight <= 0) || (max_inflight > SELF_TEST_MAX_INFLIGHT)) {
		printf("Invalid --max-inflight-rpcs argument\n"
		       "  Expected value in range (0:%d], got %d\n",
		       SELF_TEST_MAX_INFLIGHT, max_inflight);
		C_GOTO(cleanup, ret = -CER_INVAL);
	}

	/*
	 * No reason to have max_inflight bigger than the total number of RPCs
	 * each session
	 */
	max_inflight = max_inflight > rep_count ? rep_count : max_inflight;

	/********************* Print out parameters *********************/
	printf("Self Test Parameters:\n"
	       "  Group name to test against: %s\n"
	       "  # endpoints:                %u\n"
	       "  Message sizes:              [", dest_name, num_endpts);
	for (j = 0; j < num_msg_sizes; j++) {
		if (j > 0)
			printf(", ");
		printf("(%d-%s %d-%s)", all_params[j].send_size,
		       crt_st_msg_type_str[all_params[j].send_type],
		       all_params[j].reply_size,
		       crt_st_msg_type_str[all_params[j].reply_type]);
	}
	printf("]\n"
	       "  Repetitions per size:       %d\n"
	       "  Max inflight RPCs:          %d\n\n",
	       rep_count, max_inflight);

	/********************* Run the self test *********************/
	ret = run_self_test(all_params, num_msg_sizes, rep_count,
			    max_inflight, dest_name, endpts, num_endpts);

	/********************* Clean up *********************/
cleanup:
	if (all_params != NULL)
		C_FREE(all_params,
		       num_msg_sizes * sizeof(all_params[0]));

	return ret;
}
