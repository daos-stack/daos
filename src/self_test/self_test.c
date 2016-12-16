/* Copyright (C) 2016 Intel Corporation
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

struct st_ping_cb_args {
	crt_endpoint_t		dest_ep;
	crt_context_t		crt_ctx;
	crt_group_t		*srv_grp;

	/* Target number of RPCs */
	int			rep_count;

	/* Message size of current RPC workload */
	int			current_msg_size;

	/* Used to track how many RPCs have been sent so far */
	int			rep_idx;
	/* Used to track how many RPCs have been handled so far */
	int			done_count;
	/* Used to protect both of the above counts */
	pthread_spinlock_t	rep_idx_lock;

	/* Scratchpad buffer allocated by main loop for callback to use */
	char			*ping_payload;

	/* Used to measure individial RPC latencies */
	int64_t			*latencies;
};

struct st_ping_cb_data {
	/* Static arguments that are the same for all RPCs in this run */
	struct st_ping_cb_args	*cb_args;

	int			rep_idx;
	struct timespec		sent_time;
};

/* Global shutdown flag, used to terminate the progress thread */
static int g_shutdown_flag;

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
	crt_rpc_t		*ping_rpc;
	crt_rpc_t		*new_ping_rpc;

	struct st_ping_cb_data	*cb_data = (struct st_ping_cb_data *)
					   cb_info->cci_arg;
	struct st_ping_cb_args	*cb_args = cb_data->cb_args;
	struct crt_st_ping_res	*res = NULL;
	struct crt_st_ping_args	*ping_args = NULL;

	struct timespec		now;
	int			local_rep;
	int			ret;

	/* Get the RPC handle */
	ping_rpc = cb_info->cci_rpc;

	/* Check for transport errors */
	if (cb_info->cci_rc != 0)
		return cb_info->cci_rc;

	/* Get the response message */
	/*
	 * TODO: This will be used in a future commit to validate RPC
	 * contents are correct
	 */
	res = (struct crt_st_ping_res *)crt_reply_get(ping_rpc);
	if (res == NULL) {
		C_ERROR("could not get ping reply");
		return -CER_INVAL;
	}

	/* Got a real reply - record latency of this call */
	crt_gettime(&now);

	/* Record latency of this transaction */
	cb_args->latencies[cb_data->rep_idx] =
		crt_timediff_ns(&cb_data->sent_time, &now);

	/* Get an index for a message that still needs to be sent */
	pthread_spin_lock(&cb_args->rep_idx_lock);
	cb_args->done_count += 1;
	local_rep = cb_args->rep_idx;
	if (cb_args->rep_idx < cb_args->rep_count)
		cb_args->rep_idx += 1;
	pthread_spin_unlock(&cb_args->rep_idx_lock);

	/* Only send another message if one is left to send */
	if (local_rep >= cb_args->rep_count)
		return 0;

	/* Re-use payload data memory, set arguments */
	cb_data->rep_idx = local_rep;
	ret = crt_gettime(&cb_data->sent_time);
	if (ret != 0) {
		C_ERROR("crt_gettime failed; ret = %d\n", ret);
		return ret;
	}

	/* Start a new RPC request */
	ret = crt_req_create(cb_args->crt_ctx, cb_args->dest_ep,
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

static void *progress_fn(void *arg)
{
	int		ret;
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

	/*
	 * TODO: Need to figure out how to address nodes in the general case
	 * It isn't clear what the correct way is to connect the self test
	 * client to a self-test enabled CART application. Singleton seems to
	 * require making all apps allow singleton connections, and the current
	 * system involves the client having a group. It also isn't clear how
	 * subgroups will be addressed
	 */
	cb_args->dest_ep.ep_grp = cb_args->srv_grp;
	cb_args->dest_ep.ep_rank = 0;
	cb_args->dest_ep.ep_tag = 0;

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
	return *((int64_t *)a) > *((int64_t *)b);
}

static int run_self_test(int msg_sizes[], int num_msg_sizes,
			 int rep_count, int max_inflight,
			 char *dest_name)
{
	pthread_t		tid;

	crt_rpc_t		*ping_rpc = NULL;
	int			current_msg_size = 0;
	int			size_idx;
	int			ret;
	int			cleanup_ret;

	int64_t			latency_avg;
	double			latency_std_dev;
	double			throughput;
	double			bandwidth;
	struct timespec		time_start_size, time_stop_size;

	struct st_ping_cb_data	*cb_data = NULL;
	struct crt_st_ping_args	*ping_args = NULL;

	/* Static arguments (same for each RPC callback function) */
	struct st_ping_cb_args	cb_args = {{0}};
	/* Private arguments data for all RPC callback functions */
	struct st_ping_cb_data	*cb_data_alloc = NULL;

	/* Set the callback data which will be the same for all callbacks */
	cb_args.rep_count = 1; /* First run only sends one message */
	pthread_spin_init(&cb_args.rep_idx_lock, PTHREAD_PROCESS_PRIVATE);

	/* Initialize CART */
	ret = self_test_init(&cb_args, &tid, dest_name);
	if (ret != 0) {
		C_ERROR("self_test_init failed; ret = %d\n", ret);
		C_GOTO(cleanup, ret);
	}

	/* Allocate a buffer for latency measurements */
	C_ALLOC(cb_args.latencies, rep_count * sizeof(cb_args.latencies[0]));
	if (cb_args.latencies == NULL)
		C_GOTO(cleanup, ret = -CER_NOMEM);

	/* Allocate a buffer for arguments to the callback function */
	C_ALLOC(cb_data_alloc, max_inflight * sizeof(struct st_ping_cb_data));
	if (cb_data_alloc == NULL)
		C_GOTO(cleanup, ret = -CER_NOMEM);

	/*
	 * Note this starts at -1, which is a special case for measuring
	 * the startup latency
	 */
	for (size_idx = -1; size_idx < num_msg_sizes; size_idx++) {
		int local_rep;
		int inflight_idx;
		void *realloced_mem;

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
			cb_args.latencies[local_rep] = -1;

		/* Record the time right when we start processing this size */
		ret = crt_gettime(&time_start_size);
		if (ret != 0) {
			C_ERROR("crt_gettime failed; ret = %d\n", ret);
			C_GOTO(cleanup, ret);
		}

		/* Restart the RPCs completed counters */
		cb_args.done_count = 0;
		cb_args.rep_idx = 0;

		for (inflight_idx = 0; inflight_idx < max_inflight;
		     inflight_idx++) {
			/* Get an index for a message that needs to be sent */
			pthread_spin_lock(&cb_args.rep_idx_lock);
			local_rep = cb_args.rep_idx;
			if (cb_args.rep_idx < cb_args.rep_count)
				cb_args.rep_idx++;
			pthread_spin_unlock(&cb_args.rep_idx_lock);

			/* Quota met elsewhere, abort */
			if (local_rep >= cb_args.rep_count)
				break;

			/* Set payload data */
			cb_data = &cb_data_alloc[inflight_idx];
			cb_data->cb_args = &cb_args;
			cb_data->rep_idx = local_rep;
			ret = crt_gettime(&cb_data->sent_time);
			if (ret != 0) {
				C_ERROR("crt_gettime failed; ret = %d\n", ret);
				C_GOTO(cleanup, ret);
			}

			/* Start a new RPC request */
			ret = crt_req_create(cb_args.crt_ctx, cb_args.dest_ep,
					     CRT_OPC_SELF_TEST_PING, &ping_rpc);
			if (ret != 0) {
				C_ERROR("crt_req_create failed; ret = %d\n",
					ret);
				C_GOTO(cleanup, ret);
			}
			C_ASSERTF(ping_rpc != NULL,
				  "crt_req_create succeeded but RPC is NULL\n");

			/* Set the RPC arguments */
			ping_args = (struct crt_st_ping_args *)
				    crt_req_get(ping_rpc);
			C_ASSERTF(ping_args != NULL,
				  "crt_req_get returned NULL\n");
			crt_iov_set(&ping_args->ping_buf, cb_args.ping_payload,
				    current_msg_size);

			/* Send the RPC */
			ret = crt_req_send(ping_rpc, ping_response_cb, cb_data);
			if (ret != 0) {
				C_ERROR("crt_req_send failed; ret = %d\n", ret);
				C_GOTO(cleanup, ret);
			}
		}

		/* Wait until all the RPCs come back */
		while (cb_args.done_count < cb_args.rep_count)
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
			       cb_args.latencies[0] / 1000);

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
		qsort(cb_args.latencies, cb_args.rep_count,
		      sizeof(cb_args.latencies[0]), st_compare_latencies);

		/* Compute average and standard deviation*/
		latency_avg = 0;
		for (local_rep = 0; local_rep < cb_args.rep_count; local_rep++)
			latency_avg += cb_args.latencies[local_rep];
		latency_avg /= cb_args.rep_count;

		latency_std_dev = 0;
		for (local_rep = 0; local_rep < cb_args.rep_count; local_rep++)
			latency_std_dev +=
				pow(cb_args.latencies[local_rep] - latency_avg,
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
		       cb_args.latencies[0] / 1000,
		       cb_args.latencies[cb_args.rep_count / 4] / 1000,
		       cb_args.latencies[cb_args.rep_count / 2] / 1000,
		       cb_args.latencies[cb_args.rep_count * 3 / 4] / 1000,
		       cb_args.latencies[cb_args.rep_count - 1] / 1000,
		       latency_avg/1000, latency_std_dev/1000);
		printf("\n");
	}

	/* Tell the progress thread to abort and exit */
	g_shutdown_flag = 1;

	ret = pthread_join(tid, NULL);
	if (ret)
		C_ERROR("Could not join progress thread");

cleanup:
	if (cb_args.latencies != NULL)
		C_FREE(cb_args.latencies,
		       rep_count * sizeof(cb_args.latencies[0]));
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
	printf("Usage: %s --group-name <name> [--message-sizes <a,b,c,d,...>] [--repetitions-per-size <N>] [--max-inflight-rpcs <N>]\n"
	       "\n"
	       "Required Arguments\n"
	       "  --group-name <group_name>\n"
	       "      Short version: -g\n"
	       "      The name of the process set to test against\n"
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

int main(int argc, char *argv[])
{
	/* Default parameters */
	char		default_msg_sizes_str[] = "1000000,100000,10000,1000,1";
	const int	default_rep_count = 200000;
	const int	default_max_inflight = 1000;

	char		*dest_name = NULL;
	const char	tokens[] = " ,.-\t";
	char		*msg_sizes_str = default_msg_sizes_str;
	int		rep_count = default_rep_count;
	int		max_inflight = default_max_inflight;
	int		*msg_sizes = NULL;
	char		*sizes_ptr = NULL;
	char		*pch = NULL;
	int		num_msg_sizes;
	int		num_tokens;
	int		c;
	int		j;
	int		ret;

	/********************* Parse user arguments *********************/
	while (1) {
		static struct option long_options[] = {
			{"group-name", required_argument, 0, 'g'},
			{"message-sizes", required_argument, 0, 's'},
			{"repetitions-per-size", required_argument, 0, 'r'},
			{"max-inflight-rpcs", required_argument, 0, 'i'},
			{0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "g:s:r:i:", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'g':
			dest_name = optarg;
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
	if (dest_name == NULL) {
		printf("--group-name argument not specified\n");
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
	       "  Message sizes:              [", dest_name);
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
			    dest_name);

	/********************* Clean up *********************/
cleanup:
	if (msg_sizes != NULL)
		C_FREE(msg_sizes, num_msg_sizes * sizeof(msg_sizes[0]));

	return ret;
}
