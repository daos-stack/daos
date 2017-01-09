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
#define CRT_OPC_SELF_TEST_PING 0xFFFF0200UL

/* RPC arguments */
struct crt_st_ping_args {
	crt_iov_t ping_buf;
};

struct crt_st_ping_res {
	crt_iov_t resp_buf;
};

/*
 * The following three structures are duplicated in crt_rpc.c
 * Eventually they won't be needed here when the self test client is also
 * integrated into CART
 */
static struct crt_msg_field *crt_st_ping_input[] = {
	&CMF_IOVEC,
};
static struct crt_msg_field *crt_st_ping_output[] = {
	&CMF_IOVEC,
};
static struct crt_req_format CQF_CRT_SELF_TEST_PING =
	DEFINE_CRT_REQ_FMT("CRT_SELF_TEST_PING",
			   crt_st_ping_input,
			   crt_st_ping_output);

int crt_validate_grpid(const crt_group_id_t grpid);
int
crt_rpc_reg_internal(crt_opcode_t opc, struct crt_req_format *crf,
		     crt_rpc_cb_t rpc_handler, struct crt_corpc_ops *co_ops);
/*
 * TODO: DELETE REGION END
 */

/* User input maximum values */
#define SELF_TEST_MAX_MSG_SIZE (0x40000000)
#define SELF_TEST_MAX_REPETITIONS (0x40000000)
#define SELF_TEST_MAX_INFLIGHT (0x40000000)
#define SELF_TEST_MAX_LIST_STR_LEN (1 << 16)
#define SELF_TEST_MAX_NUM_ENDPOINTS (UINT32_MAX)

struct st_latency {
	int64_t val;
	uint32_t rank;
	uint32_t tag;
};

struct st_endpoint {
	uint32_t rank;
	uint32_t tag;
};

struct st_ping_cb_args {
	crt_context_t		 crt_ctx;
	crt_group_t		*srv_grp;

	/* Target number of RPCs */
	int			 rep_count;

	/* Message size of current RPC workload */
	int			 current_msg_size;

	/*
	 * Used to track how many RPCs have been sent so far
	 * NOTE: Write-protected by cb_args_lock
	 */
	int			 rep_idx;

	/*
	 * Used to track how many RPCs have been handled so far
	 * NOTE: Write-protected by cb_args_lock
	 */
	int			 rep_completed_count;

	/* Used to protect counters in this structure across threads */
	pthread_spinlock_t	 cb_args_lock;

	/* Scratchpad buffer allocated by main loop for callback to use */
	char			*ping_payload;

	/* Used to measure individial RPC latencies */
	struct st_latency	*rep_latencies;

	/* List of endpoints to test against */
	struct st_endpoint	*endpts;

	/* Number of endpoints in the endpts array */
	uint32_t		 num_endpts;

	/*
	 * Last used endpoint index
	 * NOTE: Write-protected by cb_args_lock
	 */
	uint32_t		 next_endpt_idx;

};

struct st_ping_cb_data {
	/* Static arguments that are the same for all RPCs in this run */
	struct st_ping_cb_args	*cb_args;

	int			 rep_idx;
	struct timespec		 sent_time;
};

/* Global shutdown flag, used to terminate the progress thread */
static int g_shutdown_flag;

/* Forward Declarations */
static int ping_response_cb(const struct crt_cb_info *cb_info);

static int send_next_rpc(struct st_ping_cb_data *cb_data, int is_init)
{
	struct st_ping_cb_args	*cb_args = cb_data->cb_args;

	crt_rpc_t		*new_ping_rpc;
	struct crt_st_ping_args	*ping_args = NULL;
	crt_endpoint_t		 local_endpt = {.ep_grp = cb_args->srv_grp};

	int			 local_rep;
	int			 ret;

	/******************** LOCK: cb_args_lock ********************/
	pthread_spin_lock(&cb_args->cb_args_lock);

	/* Only mark completion of an RPC if not doing initial generation */
	if (is_init == 0)
		cb_args->rep_completed_count += 1;

	/* Get an index for a message that still needs to be sent */
	local_rep = cb_args->rep_idx;
	if (cb_args->rep_idx < cb_args->rep_count)
		cb_args->rep_idx += 1;

	/* Get the next endpoint to send a message to */
	local_endpt.ep_rank = cb_args->endpts[cb_args->next_endpt_idx].rank;
	local_endpt.ep_tag = cb_args->endpts[cb_args->next_endpt_idx].tag;
	cb_args->next_endpt_idx++;
	if (cb_args->next_endpt_idx >= cb_args->num_endpts)
		cb_args->next_endpt_idx = 0;

	pthread_spin_unlock(&cb_args->cb_args_lock);
	/******************* UNLOCK: cb_args_lock *******************/

	/* Only send another message if one is left to send */
	if (local_rep >= cb_args->rep_count)
		return 0;

	/* Re-use payload data memory, set arguments */
	cb_data->rep_idx = local_rep;

	/*
	 * For the repetition we are just now generating, set which rank/tag
	 * this upcoming latency measurement will be for
	 */
	cb_args->rep_latencies[cb_data->rep_idx].rank = local_endpt.ep_rank;
	cb_args->rep_latencies[cb_data->rep_idx].tag = local_endpt.ep_tag;

	ret = crt_gettime(&cb_data->sent_time);
	if (ret != 0) {
		C_ERROR("crt_gettime failed; ret = %d\n", ret);
		return ret;
	}

	/* Start a new RPC request */
	ret = crt_req_create(cb_args->crt_ctx, local_endpt,
			     CRT_OPC_SELF_TEST_PING, &new_ping_rpc);
	if (ret != 0) {
		C_ERROR("crt_req_create failed; ret = %d\n", ret);
		return ret;
	}
	C_ASSERTF(new_ping_rpc != NULL,
		  "crt_req_create succeeded but RPC is NULL\n");

	/* Set the RPC arguments */
	ping_args = (struct crt_st_ping_args *)crt_req_get(new_ping_rpc);
	C_ASSERTF(ping_args != NULL, "crt_req_get returned NULL\n");
	crt_iov_set(&ping_args->ping_buf, cb_args->ping_payload,
		    cb_args->current_msg_size);

	/* Send the RPC */
	ret = crt_req_send(new_ping_rpc, ping_response_cb, cb_data);
	if (ret != 0) {
		C_ERROR("crt_req_send failed; ret = %d\n", ret);
		return ret;
	}

	return 0;
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
static int ping_response_cb(const struct crt_cb_info *cb_info)
{
	struct st_ping_cb_data	*cb_data = (struct st_ping_cb_data *)
					   cb_info->cci_arg;
	struct st_ping_cb_args	*cb_args = cb_data->cb_args;
	struct crt_st_ping_res	*res = NULL;

	struct timespec		 now;

	/* Check for transport errors */
	if (cb_info->cci_rc != 0)
		return cb_info->cci_rc;

	/* Get the response message */
	/*
	 * TODO: This will be used in a future commit to validate RPC
	 * contents are correct
	 */
	res = (struct crt_st_ping_res *)crt_reply_get(cb_info->cci_rpc);
	if (res == NULL) {
		C_ERROR("could not get ping reply");
		return -CER_UNKNOWN;
	}

	/* Got a valid reply - record latency of this call */
	crt_gettime(&now);

	/* Record latency of this transaction */
	cb_args->rep_latencies[cb_data->rep_idx].val =
		crt_timediff_ns(&cb_data->sent_time, &now);

	send_next_rpc(cb_data, 0);

	return 0;
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

static int self_test_init(struct st_ping_cb_args *cb_args, pthread_t *tid,
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
	C_ASSERTF(cb_args->srv_grp != NULL,
		  "crt_group_attach succeeded but returned group is NULL\n");

	ret = crt_context_create(NULL, &cb_args->crt_ctx);
	if (ret != 0) {
		C_ERROR("crt_context_create failed; ret = %d\n", ret);
		return ret;
	}

	/* Register RPCs */
	ret = crt_rpc_reg_internal(CRT_OPC_SELF_TEST_PING,
				   &CQF_CRT_SELF_TEST_PING, NULL, NULL);
	if (ret != 0) {
		C_ERROR("ping registration failed; ret = %d\n", ret);
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

static int run_self_test(int msg_sizes[], int num_msg_sizes, int rep_count,
			 int max_inflight, char *dest_name,
			 struct st_endpoint *endpts, uint32_t num_endpts)
{
	pthread_t		 tid;

	int			 current_msg_size = 0;
	int			 size_idx;
	int			 ret;
	int			 cleanup_ret;

	int64_t			 latency_avg;
	double			 latency_std_dev;
	double			 throughput;
	double			 bandwidth;
	struct timespec		 time_start_size, time_stop_size;

	/* Static arguments (same for each RPC callback function) */
	struct st_ping_cb_args	 cb_args = {0};
	/* Private arguments data for all RPC callback functions */
	struct st_ping_cb_data	*cb_data_alloc = NULL;

	/* Set the callback data which will be the same for all callbacks */
	cb_args.rep_count = 1; /* First run only sends one message */
	pthread_spin_init(&cb_args.cb_args_lock, PTHREAD_PROCESS_PRIVATE);

	/* Initialize CART */
	ret = self_test_init(&cb_args, &tid, dest_name);
	if (ret != 0) {
		C_ERROR("self_test_init failed; ret = %d\n", ret);
		C_GOTO(cleanup, ret);
	}

	/* Allocate a buffer for latency measurements */
	C_ALLOC(cb_args.rep_latencies,
		rep_count * sizeof(cb_args.rep_latencies[0]));
	if (cb_args.rep_latencies == NULL)
		C_GOTO(cleanup, ret = -CER_NOMEM);

	/* Allocate a buffer for arguments to the callback function */
	C_ALLOC(cb_data_alloc, max_inflight * sizeof(struct st_ping_cb_data));
	if (cb_data_alloc == NULL)
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
		void	*realloced_mem;

		if (size_idx == -1) {
			current_msg_size = 1;
		} else {
			current_msg_size = msg_sizes[size_idx];
			cb_args.rep_count = rep_count;
		}

		/*
		 * Use one buffer for all of the ping payload contents
		 * realloc() used so memory will be reused if size decreases
		 */
		realloced_mem = C_REALLOC(cb_args.ping_payload,
					  current_msg_size);
		if (realloced_mem == NULL)
			C_GOTO(cleanup, ret = -CER_NOMEM);
		cb_args.ping_payload = (char *)realloced_mem;
		memset(cb_args.ping_payload, 0, current_msg_size);

		/* Set remaining callback data argument that changes per test */
		cb_args.current_msg_size = current_msg_size;

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

		for (inflight_idx = 0; inflight_idx < max_inflight;
		     inflight_idx++) {
			struct st_ping_cb_data *cb_data =
				&cb_data_alloc[inflight_idx];

			cb_data->cb_args = &cb_args;
			cb_data->rep_idx = -1;

			send_next_rpc(cb_data, 1);
		}

		/* Wait until all the RPCs come back */
		while (cb_args.rep_completed_count < cb_args.rep_count)
			sched_yield();

		/* Record the time right when we stopped processing this size */
		ret = crt_gettime(&time_stop_size);
		if (ret != 0) {
			C_ERROR("crt_gettime failed; ret = %d\n", ret);
			C_GOTO(cleanup, ret);
		}

		/* Print out the first message latency separately */
		if (size_idx == -1) {
			printf("First RPC latency (size=1) (us): %ld\n\n",
			       cb_args.rep_latencies[0].val / 1000);

			continue;
		}

		/* Compute the throughput and bandwidth for this size */
		throughput = cb_args.rep_count /
			(crt_timediff_ns(&time_start_size, &time_stop_size) /
			 1000000000.0F);
		bandwidth = throughput * current_msg_size;

		/* Print the results for this size */
		printf("Results for message size %d (max_inflight_rpcs = %d)\n",
		       current_msg_size, max_inflight);
		printf("\tRPC Bandwidth (MB/sec): %.2f\n",
		       bandwidth / 1000000.0F);
		printf("\tRPC Throughput (RPCs/sec): %.0f\n", throughput);

		/*
		 * TODO:
		 * In the future, probably want to return the latencies here
		 * before they are sorted
		 */

		/* Sort the latencies */
		qsort(cb_args.rep_latencies, cb_args.rep_count,
		      sizeof(cb_args.rep_latencies[0]), st_compare_latencies);

		/* Compute average and standard deviation*/
		latency_avg = 0;
		for (local_rep = 0; local_rep < cb_args.rep_count; local_rep++)
			latency_avg += cb_args.rep_latencies[local_rep].val;
		latency_avg /= cb_args.rep_count;

		latency_std_dev = 0;
		for (local_rep = 0; local_rep < cb_args.rep_count; local_rep++)
			latency_std_dev +=
				pow(cb_args.rep_latencies[local_rep].val -
				    latency_avg,
				    2);
		latency_std_dev /= cb_args.rep_count;
		latency_std_dev = sqrt(latency_std_dev);

		/* Print Latency Results */
		printf("\tRPC Latencies (us):\n"
		       "\t\tMin    : %ld\n"
		       "\t\t25th  %%: %ld\n"
		       "\t\tMedian : %ld\n"
		       "\t\t75th  %%: %ld\n"
		       "\t\tMax    : %ld\n"
		       "\t\tAverage: %ld\n"
		       "\t\tStd Dev: %.2f\n",
		       cb_args.rep_latencies[0].val / 1000,
		       cb_args.rep_latencies[cb_args.rep_count / 4].val / 1000,
		       cb_args.rep_latencies[cb_args.rep_count / 2].val / 1000,
		       cb_args.rep_latencies[cb_args.rep_count*3/4].val / 1000,
		       cb_args.rep_latencies[cb_args.rep_count - 1].val / 1000,
		       latency_avg / 1000, latency_std_dev / 1000);
		printf("\n");
	}

	/* Tell the progress thread to abort and exit */
	g_shutdown_flag = 1;

	ret = pthread_join(tid, NULL);
	if (ret)
		C_ERROR("Could not join progress thread");

cleanup:
	if (cb_args.rep_latencies != NULL)
		C_FREE(cb_args.rep_latencies,
		       rep_count * sizeof(cb_args.rep_latencies[0]));
	if (cb_data_alloc != NULL)
		C_FREE(cb_data_alloc,
		       max_inflight * sizeof(struct st_ping_cb_data));
	if (cb_args.ping_payload != NULL)
		C_FREE(cb_args.ping_payload, current_msg_size);

	cleanup_ret = crt_context_destroy(cb_args.crt_ctx, 0);
	if (cleanup_ret != 0)
		C_ERROR("crt_context_destroy failed; ret = %d\n", cleanup_ret);
	/* Make sure first error is returned, if applicable */
	ret = ((ret == 0) ? cleanup_ret : ret);

	if (cb_args.srv_grp != NULL) {
		cleanup_ret = crt_group_detach(cb_args.srv_grp);
		if (cleanup_ret != 0)
			C_ERROR("crt_group_detach failed; ret = %d\n",
				cleanup_ret);
		/* Make sure first error is returned, if applicable */
		ret = ((ret == 0) ? cleanup_ret : ret);
	}

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
	       "  --message-sizes <a,b,c,d>\n"
	       "      Short version: -s\n"
	       "      List of ping sizes in bytes to use for the self test.\n"
	       "      Performance results will be reported individually for each size.\n"
	       "      Default: \"%s\"\n"
	       "\n"
	       "  --repetitions-per-size <N>\n"
	       "      Short version: -r\n"
	       "      Number of samples per message size. Pings for each particular size\n"
	       "      will be repeated this many times.\n"
	       "      Default: %d\n"
	       "\n"
	       "  --max-inflight-rpcs <N>\n"
	       "      Short version: -i\n"
	       "      Maximum number of RPCs allowed to be executing concurrently.\n"
	       "      Default: %d\n",
	       prog_name, msg_sizes_str, rep_count, max_inflight);
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
	char *saveptr_comma;
	char *saveptr_hyphen;
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

int main(int argc, char *argv[])
{
	/* Default parameters */
	char			 default_msg_sizes_str[] = "10000,1000,1";
	const int		 default_rep_count = 20000;
	const int		 default_max_inflight = 1000;

	char			*dest_name = NULL;
	const char		 tokens[] = " ,.-\t";
	char			*msg_sizes_str = default_msg_sizes_str;
	int			 rep_count = default_rep_count;
	int			 max_inflight = default_max_inflight;
	int			*msg_sizes = NULL;
	char			*sizes_ptr = NULL;
	char			*pch = NULL;
	int			 num_msg_sizes;
	int			 num_tokens;
	int			 c;
	int			 j;
	int			 ret;
	struct st_endpoint	*endpts = NULL;
	uint32_t		 num_endpts = 0;

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
	 * Count the number of tokens in the user-specified string
	 * This gives an upper limit on the number of arguments the user passed
	 */
	num_tokens = 0;
	sizes_ptr = msg_sizes_str;
	while (1) {
		const char *token_ptr = tokens;

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
	C_ALLOC(msg_sizes, (num_tokens + 1) * sizeof(msg_sizes[0]));
	if (msg_sizes == NULL)
		C_GOTO(cleanup, ret = -CER_NOMEM);

	/* Iterate over the user's message sizes and parse / validate them */
	num_msg_sizes = 0;
	pch = strtok(msg_sizes_str, tokens);
	while (pch != NULL) {
		C_ASSERTF(num_msg_sizes <= num_tokens, "Token counting err\n");
		ret = sscanf(pch, "%d", &msg_sizes[num_msg_sizes]);

		if ((msg_sizes[num_msg_sizes] <= 0)
		    || (msg_sizes[num_msg_sizes] >
			SELF_TEST_MAX_MSG_SIZE)
		    || ret != 1) {
			printf("Warning: Invalid max-message-sizes token\n"
			       "  Expected value in range (0:%d], got %s\n",
			       SELF_TEST_MAX_MSG_SIZE,
			       pch);
		} else {
			num_msg_sizes++;
		}

		pch = strtok(NULL, tokens);
	}

	if (num_msg_sizes <= 0) {
		printf("No valid message sizes given\n");
		C_GOTO(cleanup, ret = -CER_INVAL);
	}

	/* Shrink the buffer if some of the user's tokens weren't kept */
	if (num_msg_sizes < num_tokens + 1) {
		void *realloced_mem;

		/* This should always succeed since the buffer is shrinking.. */
		realloced_mem = C_REALLOC(msg_sizes,
					  num_msg_sizes * sizeof(msg_sizes[0]));
		if (realloced_mem == NULL)
			C_GOTO(cleanup, ret = -CER_NOMEM);
		msg_sizes = (int *)realloced_mem;
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

	/********************* Print out parameters *********************/
	printf("Self Test Parameters:\n"
	       "  Group name to test against: %s\n"
	       "  # endpoints:                %u\n"
	       "  Message sizes:              [", dest_name, num_endpts);
	for (j = 0; j < num_msg_sizes; j++) {
		if (j > 0)
			printf(", ");
		printf("%d", msg_sizes[j]);
	}
	printf("]\n"
	       "  Repetitions per size:       %d\n"
	       "  Max inflight RPCs:          %d\n\n",
	       rep_count, max_inflight);

	/********************* Run the self test *********************/
	ret = run_self_test(msg_sizes, num_msg_sizes, rep_count, max_inflight,
			    dest_name, endpts, num_endpts);

	/********************* Clean up *********************/
cleanup:
	if (msg_sizes != NULL)
		C_FREE(msg_sizes, num_msg_sizes * sizeof(msg_sizes[0]));

	return ret;
}
