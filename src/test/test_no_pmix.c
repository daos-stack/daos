/* Copyright (C) 2018-2019 Intel Corporation
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
/**
 * This is a simple test for situations when PMIX is disabled
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <gurt/common.h>
#include <cart/api.h>


struct test_options {
	int		self_rank;
	bool		is_master;
	d_rank_list_t	group_ranks;
	char		*uri_file_prefix;
};

static struct test_options opts;
static crt_context_t crt_ctx;
static crt_context_t aux_ctx;
static crt_group_t	*g_group;

static bool g_do_shutdown;
static int issue_test_ping(d_rank_t target_rank, int target_tag);
static pid_t mypid;

#define DBG_PRINT(x...) \
	do { \
		fprintf(stderr, "[rank=%d pid=%d]\t", opts.self_rank, mypid); \
		fprintf(stderr, x); \
	} while (0)

#define RPC_REGISTER(name) \
	CRT_RPC_SRV_REGISTER(name, 0, name, DQF_FUNC_##name)

#define CORPC_REGISTER(name, ops) \
	CRT_RPC_CORPC_REGISTER(name, name, DQF_FUNC_##name, ops)

#define RPC_DECLARE(name, function)					\
	CRT_RPC_DECLARE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)		\
	CRT_RPC_DEFINE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)		\
	static void *DQF_FUNC_##name = (void *)function

enum {
	RPC_TEST_PING = 0xB1,
	RPC_TEST_INDIRECT_PING = 0xB2,
	CORPC_TEST_PING = 0xC1,
	RPC_SET_GRP_INFO = 0xC2,
	RPC_SHUTDOWN = 0xE0,
} rpc_id_t;

#define CRT_ISEQ_RPC_TEST_PING	/* input fields */		 \
	((uint64_t)		(field)			CRT_VAR)

#define CRT_OSEQ_RPC_TEST_PING	/* output fields */		 \
	((uint64_t)		(field)			CRT_VAR)

#define CRT_ISEQ_CORPC_TEST_PING /* input fields */		 \
	((uint64_t)		(field)			CRT_VAR)

#define CRT_OSEQ_CORPC_TEST_PING /* output fields */		 \
	((uint64_t)		(field)			CRT_VAR)

#define CRT_ISEQ_RPC_SET_GRP_INFO /* input fields */		 \
	((d_iov_t)		(grp_info)		CRT_VAR)

#define CRT_OSEQ_RPC_SET_GRP_INFO /* output fields */		 \
	((uint64_t)		(rc)			CRT_VAR)

#define CRT_ISEQ_RPC_TEST_INDIRECT_PING	/* input fields */	 \
	((d_rank_t)		(rank_to_ping)		CRT_VAR)

#define CRT_OSEQ_RPC_TEST_INDIRECT_PING	/* output fields */	 \
	((uint64_t)		(field)			CRT_VAR)

#define CRT_ISEQ_RPC_SHUTDOWN	/* input fields */		 \
	((uint64_t)		(field)			CRT_VAR)

#define CRT_OSEQ_RPC_SHUTDOWN	/* output fields */		 \
	((uint64_t)		(field)			CRT_VAR)

static int test_ping_hdlr(crt_rpc_t *rpc);
static void corpc_test_ping_hdlr(crt_rpc_t *rpc);
static int set_grp_info_hdlr(crt_rpc_t *rpc);
static int test_ping_indirect_hdlr(crt_rpc_t *rpc);
static int shutdown_hdlr(crt_rpc_t *rpc);

RPC_DECLARE(RPC_TEST_PING, test_ping_hdlr);
RPC_DECLARE(CORPC_TEST_PING, corpc_test_ping_hdlr);

RPC_DECLARE(RPC_SET_GRP_INFO, set_grp_info_hdlr);

RPC_DECLARE(RPC_TEST_INDIRECT_PING, test_ping_indirect_hdlr);

RPC_DECLARE(RPC_SHUTDOWN, shutdown_hdlr);

static int
shutdown_hdlr(crt_rpc_t *rpc)
{
	DBG_PRINT("Initiaing shutdown sequence...\n");
	g_do_shutdown = 1;
	crt_reply_send(rpc);
	return 0;
}

static int
set_grp_info_hdlr(crt_rpc_t *rpc)
{
	struct RPC_SET_GRP_INFO_in	*input;
	struct RPC_SET_GRP_INFO_out	*output;

	input = crt_req_get(rpc);
	output = crt_reply_get(rpc);

	crt_group_info_set(&input->grp_info);

	output->rc = 0;
	crt_reply_send(rpc);


	return 0;
}

static void
generic_response_hdlr(const struct crt_cb_info *info)
{
	crt_rpc_t	*rpc_req = NULL;
	int		*done;

	rpc_req = info->cci_rpc;

	crt_req_addref(rpc_req);
	done = info->cci_arg;

	*done = 1;
}


static int
issue_set_grp_info(d_rank_t target_rank, int target_tag,
		d_iov_t *iov)
{
	struct RPC_SET_GRP_INFO_in	*input;
	crt_rpc_t			*rpc_req;
	crt_endpoint_t			server_ep;
	int rc;
	int done;

	server_ep.ep_rank = target_rank;
	server_ep.ep_tag = target_tag;
	server_ep.ep_grp = NULL;

	DBG_PRINT("SENDING GRP_INFO TO %d:%d\n", target_rank, target_tag);

	rc = crt_req_create(crt_ctx, &server_ep, RPC_SET_GRP_INFO,
			&rpc_req);
	if (rc != 0) {
		D_ERROR("crt_req_create() failed; rc=%d\n", rc);
		assert(0);
	}

	input = crt_req_get(rpc_req);
	input->grp_info = *iov;

	done = 0;

	rc = crt_req_send(rpc_req, generic_response_hdlr, &done);
	if (rc != 0) {
		D_ERROR("crt_req_send() failed; rc=%d\n", rc);
		assert(0);
	}

	while (!done) {
		crt_progress(crt_ctx, 1000, 0, 0);
		sched_yield();
	}
	DBG_PRINT("Response received from %d:%d\n", target_rank, target_tag);
	crt_req_decref(rpc_req);

	return 0;
}

static void corpc_test_ping_hdlr(crt_rpc_t *rpc)
{
	struct RPC_TEST_PING_in		*input;
	struct RPC_TEST_PING_out	*output;
	int rc;

	DBG_PRINT("CORPC TEST ping handler called\n");

	input = crt_req_get(rpc);
	output = crt_reply_get(rpc);


	output->field = input->field * 2;
	rc = crt_reply_send(rpc);
	if (rc != 0) {
		D_ERROR("Failed to send response back:(\n");
		D_ASSERT(0);
	}

	DBG_PRINT("Response was sent\n");

}


void ping_response_hdlr(const struct crt_cb_info *info)
{
	crt_rpc_t			*rpc = NULL;
	struct RPC_TEST_PING_out	*output;
	struct RPC_TEST_PING_in		*input;

	DBG_PRINT("Ping respones hdlr\n");
	rpc = info->cci_arg;

	output = crt_reply_get(rpc);
	input = crt_req_get(rpc);

	output->field = input->field;

	crt_reply_send(rpc);
	crt_req_decref(rpc);
}

int test_ping_indirect_hdlr(crt_rpc_t *rpc)
{
	struct RPC_TEST_INDIRECT_PING_in	*input;
	crt_rpc_t				*tgt_req;
	crt_endpoint_t				ep;

	input = crt_req_get(rpc);

	DBG_PRINT("Received indirect ping request to ping rank=%d\n",
		input->rank_to_ping);
	crt_req_addref(rpc);

	ep.ep_rank = input->rank_to_ping;
	ep.ep_tag = 0;
	ep.ep_grp = NULL;

	crt_req_create(crt_ctx, &ep, RPC_TEST_PING, &tgt_req);
	crt_req_send(tgt_req, ping_response_hdlr, rpc);

	return 0;
}

int
issue_indirect_test_ping(d_rank_t imm_rank, int imm_tag,
		d_rank_t target_rank)
{
	struct RPC_TEST_INDIRECT_PING_in	*input;
	struct RPC_TEST_PING_out		*output;
	crt_endpoint_t				server_ep;
	crt_rpc_t				*rpc_req;
	int					done;
	int					rc;

	server_ep.ep_rank = imm_rank;
	server_ep.ep_grp = NULL;
	server_ep.ep_tag = imm_tag;

	DBG_PRINT("Indirect test ping to rank:tag=%d:%d (to ping %d)\n",
		server_ep.ep_rank, server_ep.ep_tag, target_rank);

	rc = crt_req_create(crt_ctx, &server_ep,
		RPC_TEST_INDIRECT_PING, &rpc_req);
	if (rc != 0) {
		D_ERROR("crt_req_create() failed; rc=%d\n", rc);
		assert(0);
	}

	input = crt_req_get(rpc_req);

	input->rank_to_ping = target_rank;

	done = 0;
	rc = crt_req_send(rpc_req, generic_response_hdlr, &done);
	if (rc != 0) {
		D_ERROR("crt_req_send() failed; rc=%d\n", rc);
		assert(0);
	}

	while (!done)
		sched_yield();

	output = crt_reply_get(rpc_req);
	if (output->field != input->rank_to_ping) {
		D_ERROR("Incorrect data in the output. Expected %d got %ld\n",
			input->rank_to_ping, output->field);
		assert(0);
	}

	DBG_PRINT("Reponse received, all is good\n");
	crt_req_decref(rpc_req);

	return 0;
}

static int test_ping_hdlr(crt_rpc_t *rpc)
{
	struct RPC_TEST_PING_in		*input;
	struct RPC_TEST_PING_out	*output;
	int				rc;

	DBG_PRINT("TEST_PING_HDLR called\n");
	input = crt_req_get(rpc);
	output = crt_reply_get(rpc);

	output->field = input->field * 2;

	rc = crt_reply_send(rpc);
	if (rc != 0) {
		D_ERROR("Failed to send response back:(\n");
		D_ASSERT(0);
	}

	DBG_PRINT("RESPONSE SENT\n");

	return 0;
}


static void
show_usage(void)
{
	DBG_PRINT("Usage: ./server <self_rank> [OPTIONS]\n");
	DBG_PRINT("Options:\n");
	DBG_PRINT("-m <ranks>: Master application is proivded coma "
		"separated list of ranks\n");
	DBG_PRINT("-u <uri_file_prefix>\n");
}


static int
issue_test_ping(d_rank_t target_rank, int target_tag)
{
	struct RPC_TEST_PING_in		*input;
	struct RPC_TEST_PING_out	*output;
	crt_endpoint_t			server_ep;
	crt_rpc_t			*rpc_req;
	int				done;
	int rc;

	server_ep.ep_rank = target_rank;
	server_ep.ep_grp = NULL;
	server_ep.ep_tag = target_tag;

	DBG_PRINT("Issuing test ping to rank=%d tag=%d\n",
		server_ep.ep_rank, server_ep.ep_tag);
	rc = crt_req_create(crt_ctx, &server_ep, RPC_TEST_PING, &rpc_req);
	if (rc != 0) {
		D_ERROR("crt_req_create() failed; rc=%d\n", rc);
		assert(0);
	}

	input = crt_req_get(rpc_req);

	input->field = 10;

	done = 0;
	rc = crt_req_send(rpc_req, generic_response_hdlr, &done);
	if (rc != 0) {
		D_ERROR("crt_req_send() failed; rc=%d\n", rc);
		assert(0);
	}

	while (!done)
		sched_yield();

	output = crt_reply_get(rpc_req);
	if (output->field != input->field * 2) {
		D_ERROR("Incorrect data in the output. Expected %ld got %ld\n",
			input->field * 2, output->field);
		assert(0);
	}

	DBG_PRINT("Response recieved, all is good!\n");
	crt_req_decref(rpc_req);

	return 0;
}


int
issue_shutdown(d_rank_t target_rank)
{
	crt_endpoint_t			server_ep;
	crt_rpc_t			*rpc_req;
	int				done;
	int rc;

	server_ep.ep_rank = target_rank;
	server_ep.ep_grp = NULL;
	server_ep.ep_tag = 0;

	DBG_PRINT("Issuing shutdown to rank=%d tag=%d\n",
		server_ep.ep_rank, server_ep.ep_tag);
	rc = crt_req_create(crt_ctx, &server_ep, RPC_SHUTDOWN, &rpc_req);
	if (rc != 0) {
		D_ERROR("crt_req_create() failed; rc=%d\n", rc);
		assert(0);
	}

	done = 0;
	rc = crt_req_send(rpc_req, generic_response_hdlr, &done);
	if (rc != 0) {
		D_ERROR("crt_req_send() failed; rc=%d\n", rc);
		assert(0);
	}


	while (!done)
		sched_yield();

	DBG_PRINT("Shutdown  reply recieved, all is good!\n");
	crt_req_decref(rpc_req);

	return 0;
}

int issue_test_corpc_ping(void)
{
	int rc;
	d_rank_list_t	excluded_membs;
	d_rank_t	excluded_ranks = {0};
	crt_rpc_t	*rpc;
	struct RPC_TEST_PING_in		*input;
	struct RPC_TEST_PING_out	*output;
	int done;

	excluded_ranks = opts.self_rank;
	excluded_membs.rl_nr = 1;
	excluded_membs.rl_ranks = &excluded_ranks;

	rc = crt_corpc_req_create(crt_ctx, NULL, &excluded_membs,
			CORPC_TEST_PING, NULL, 0, 0,
			crt_tree_topo(CRT_TREE_KNOMIAL, 4), &rpc);
	if (rc != 0) {
		D_ERROR("crt_corpc_req_create() failed; rc=%d\n", rc);
		assert(0);
	}

	input = crt_req_get(rpc);
	input->field = 0x31337;

	done = 0;
	rc = crt_req_send(rpc, generic_response_hdlr, &done);
	if (rc != 0) {
		D_ERROR("crt_req_send() failed; rc=%d\n", rc);
		assert(0);
	}

	DBG_PRINT("CORPC test ping issued\n");
	while (!done) {
		crt_progress(crt_ctx, 1000, 0, 0);
		sched_yield();
	}
	output = crt_reply_get(rpc);

	DBG_PRINT("Output field was %lx, expected was %lx\n",
		output->field, input->field * 2);
	crt_req_decref(rpc);

	return 0;
}

static int
parse_options(int argc, char **argv)
{
	int ch;
	char *p;
	char *rank_str;
	int indx = 0;

	while (1) {
		ch = getopt(argc, argv, "m:u:");

		if (ch == -1)
			break;

		switch (ch) {
		case 'u':
			opts.uri_file_prefix = optarg;
			break;

		case 'm':
			opts.is_master = true;

			/* Calculate number of ranks on cmd line */
			p = optarg;
			while (*p) {
				if (*p == ',')
					opts.group_ranks.rl_nr++;
				p++;
			}
			opts.group_ranks.rl_nr++;


			D_ALLOC_ARRAY(opts.group_ranks.rl_ranks,
					opts.group_ranks.rl_nr);
			if (opts.group_ranks.rl_ranks == NULL) {
				D_ERROR("Failed to allocate mem for ranks\n");
				assert(0);
			}

			p = optarg;
			rank_str = p;
			indx = 0;
			while (1) {
				if (*p == ',' || *p == '\0') {
					opts.group_ranks.rl_ranks[indx] =
								atoi(rank_str);
					indx++;

					if (*p == '\0')
						break;
					*p = '\0';
					rank_str = p + 1;
				}
				p++;
			}

			break;

		default:
			return -1;
		}
	}

	return 0;
}

/* Function parses rank_uris string and adds uris of
 * specified rank/tags
 */
static void
add_rank_uris(char *rank_uris)
{
	crt_node_info_t node_info;
	d_rank_t	rank;
	int		tag;
	char		*p, *_rank, *_uri, *_tag;
	int		rc;
	char		*ret_uri;

	p = strtok(rank_uris, "\n");

	while (p) {
		_rank = p;
		_uri = NULL;
		while (*p) {

			if (*p == '-') {
				*p = '\0';
				_tag = p+1;
			}

			if (*p == ':') {
				*p = '\0';
				_uri = p+1;
				break;
			}
			p++;
		}

		if (_uri) {
			node_info.uri = _uri;
			rank = atoi(_rank);
			tag = _tag ? atoi(_tag) : 0;

			rc = crt_group_node_add(g_group, rank, tag, node_info);
			if (rc != 0) {
				D_ERROR("crt_group_node_add() failed; rc=%d\n",
					rc);
				assert(0);
			}

			/* Get URI back and verify it */
			rc = crt_rank_uri_get(g_group, rank, tag, &ret_uri);
			if (rc != 0) {
				D_ERROR("crt_rank_uri_get() failed; rc=%d\n",
					rc);
				assert(0);
			}

			if (strcmp(_uri, ret_uri) != 0) {
				D_ERROR("Uris dont match. Got %s expect %s\n",
					ret_uri, _uri);
				assert(0);
			}

			D_FREE(ret_uri);
		}

		p = strtok(NULL, "\n");
	}

}

static int
corpc_aggregate(crt_rpc_t *src, crt_rpc_t *result, void *priv)
{
	struct RPC_TEST_PING_out	*output_src;
	struct RPC_TEST_PING_out	*output_result;


	output_src = crt_reply_get(src);
	output_result = crt_reply_get(result);

	output_result->field = output_src->field;
	return 0;
}

struct crt_corpc_ops corpc_test_ping_ops = {
	.co_aggregate = corpc_aggregate,
	.co_pre_forward = NULL,
};



static void *
progress_function(void *data)
{
	crt_context_t *p_ctx = (crt_context_t *)data;

	DBG_PRINT("Progress thread starting\n");
	while (g_do_shutdown == 0)
		crt_progress(*p_ctx, 1000, NULL, NULL);

	crt_context_destroy(*p_ctx, 1);

	return NULL;
}

static crt_group_t *secondary_grp;

static int
grp_create_cb(crt_group_t *grp, void *priv, int status)
{
	sem_t *token = (sem_t *)priv;

	DBG_PRINT("group create finished with status=%d\n", status);

	if (status != 0) {
		D_ERROR("Failed to create subgroup\n");
		assert(0);
	}

	secondary_grp = grp;
	sem_post(token);

	return 0;
}

static int
grp_destroy_cb(void *arg, int status)
{
	sem_t *token = (sem_t *)arg;

	DBG_PRINT("group destroy finished with status=%d\n", status);

	if (status != 0) {
		D_ERROR("Failed to destroy subgroup\n");
		assert(0);
	}

	secondary_grp = NULL;
	sem_post(token);

	return 0;
}

static void
sem_timed_wait(sem_t *sem, int sec, int line_number)
{
	struct timespec			deadline;
	int				rc;

	rc = clock_gettime(CLOCK_REALTIME, &deadline);
	D_ASSERTF(rc == 0, "clock_gettime() failed at line %d rc: %d\n",
		  line_number, rc);
	deadline.tv_sec += sec;
	rc = sem_timedwait(sem, &deadline);
	D_ASSERTF(rc == 0, "sem_timedwait() failed at line %d rc: %d\n",
		  line_number, rc);
}

int main(int argc, char **argv)
{
	d_rank_t	my_rank;
	uint32_t	grp_size;
	char		*my_uri;
	pthread_t	progress_thread;
	d_iov_t		grp_info;
	d_rank_list_t	*rank_list;
	bool		found;
	d_rank_t	rank;
	int		i, x, rc;
	/* Master waits for all other ranks to populate uri data in
	 * /tmp/no_pmix_rank<rank>.info
	 */
	char		*default_uri_path = "/tmp/no_pmix_rank";
	char		*uri_file_path;
	sem_t		token_to_proceed;

	if (argc < 2) {
		show_usage();
		return -1;
	}

	mypid = getpid();
	opts.self_rank = atoi(argv[1]);

	if (parse_options(argc-1, argv+1) != 0) {
		show_usage();
		return -1;
	}

	rc = d_log_init();
	assert(rc == 0);

	DBG_PRINT("Self rank = %d\n", opts.self_rank);

	rc = sem_init(&token_to_proceed, 0, 0);
	if (rc != 0) {
		D_ERROR("sem_init() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_init(0, CRT_FLAG_BIT_SERVER |
		CRT_FLAG_BIT_PMIX_DISABLE | CRT_FLAG_BIT_LM_DISABLE);
	if (rc != 0) {
		D_ERROR("crt_init() failed with rc=%d\n", rc);
		assert(0);
	}

	g_group = crt_group_lookup(NULL);
	if (!g_group) {
		D_ERROR("Failed to lookup group\n");
		assert(0);
	}

	rc = crt_context_create(&crt_ctx);
	if (rc != 0) {
		D_ERROR("crt_context_create() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_context_create(&aux_ctx);
	if (rc != 0) {
		D_ERROR("crt_context_create() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = pthread_create(&progress_thread, 0, progress_function, &crt_ctx);
	assert(rc == 0);

	rc = RPC_REGISTER(RPC_TEST_PING);
	if (rc != 0) {
		D_ERROR("RPC_REGISTER() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = RPC_REGISTER(RPC_SHUTDOWN);
	if (rc != 0) {
		D_ERROR("RPC_REGISTER() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = RPC_REGISTER(RPC_TEST_INDIRECT_PING);
	if (rc != 0) {
		D_ERROR("RPC_REGISTER() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = RPC_REGISTER(RPC_SET_GRP_INFO);
	if (rc != 0) {
		D_ERROR("RPC_REGISTER() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = CORPC_REGISTER(CORPC_TEST_PING, &corpc_test_ping_ops);
	if (rc != 0) {
		D_ERROR("crt_corpc_register() failed; rc=%d\n", rc);
		assert(0);
	}

	/* We expect error here */
	DBG_PRINT("Error log message expected on next call\n");
	rc = crt_group_rank(NULL, &my_rank);
	if (rc != -DER_NONEXIST) {
		D_ERROR("crt_group_rank() failed; Expected %d got rc=%d\n",
			-DER_NONEXIST, rc);
		assert(0);
	}

	rc = crt_rank_self_set(opts.self_rank);
	if (rc != 0) {
		D_ERROR("Setting self rank=%d failed with rc=%d\n",
			opts.self_rank, rc);
		assert(0);
	}

	rc = crt_group_rank(NULL, &my_rank);
	if (rc != 0) {
		D_ERROR("crt_group_rank() failed; rc=%d\n", rc);
		assert(0);
	}

	if (my_rank != opts.self_rank) {
		D_ERROR("self rank not set properly. Expected=%d got=%d\n",
			opts.self_rank, my_rank);
		assert(0);
	}

	rc = crt_group_size(NULL, &grp_size);
	if (rc != 0) {
		D_ERROR("crt_group_size() failed; rc=%d\n", rc);
		assert(0);
	}

	if (grp_size != 1) {
		D_ERROR("Wrong group size; expected=1 got=%d\n",
			grp_size);
		assert(0);
	}


	uri_file_path = default_uri_path;
	if (opts.is_master) {
		char		tmp_name[256];
		char		tmp_data[256];
		struct stat	file_stat;
		d_rank_t	pri_rank;
		int		fd;
		int		retries = 10;
		crt_group_t	*tmp_grp;

		if (opts.uri_file_prefix != NULL)
			uri_file_path = opts.uri_file_prefix;

		/* Wait for all uri files to be populated by other ranks */
		for (i = 0; i < opts.group_ranks.rl_nr; i++) {
			if (opts.group_ranks.rl_ranks[i] == opts.self_rank)
				continue;

			sprintf(tmp_name, "%s%d.uri_info",
				uri_file_path, opts.group_ranks.rl_ranks[i]);

			DBG_PRINT("Waiting for file |%s|\n", tmp_name);

			while (stat(tmp_name, &file_stat) != 0) {
				DBG_PRINT("File %s not present. sleep(1)\n",
					tmp_name);

				if (retries-- <= 0) {
					DBG_PRINT("Wait for files failed\n");
					assert(0);
				}
				sleep(1);
			}

			/* Read out uri data from file */
			fd = open(tmp_name, O_RDONLY);
			rc = read(fd, tmp_data, 256);
			if (rc <= 0) {
				DBG_PRINT("File %s not populated\n", tmp_name);
				assert(0);
			}
			close(fd);

			/* Parse uri data and add rank:tag:uri to group */
			add_rank_uris(tmp_data);
		}

		/* Retrieve group info as one iov */
		rc = crt_group_info_get(g_group, &grp_info);
		if (rc != 0) {
			DBG_PRINT("crt_group_info_get() failed; rc=%d\n", rc);
			assert(0);
		}

		rc = crt_group_ranks_get(g_group, &rank_list);
		if (rc != 0) {
			DBG_PRINT("crt_group_ranks_get() failed; rc=%d\n", rc);
			assert(0);
		}

		DBG_PRINT("Testing crt_group_ranks_get()\n");

		if (rank_list->rl_nr != opts.group_ranks.rl_nr) {
			DBG_PRINT("Ret size %d doesnt match expected %d\n",
				rank_list->rl_nr, opts.group_ranks.rl_nr);
			assert(0);
		}

		for (i = 0; i < rank_list->rl_nr; i++) {
			found = false;
			rank = rank_list->rl_ranks[i];

			for (x = 0; x < opts.group_ranks.rl_nr; x++) {
				if (opts.group_ranks.rl_ranks[x] == rank) {
					found = true;
					break;
				}
			}

			if (found) {
				DBG_PRINT("Rank %d found\n", rank);
			} else {
				DBG_PRINT("Rank %d not found:(\n", rank);
				assert(0);
			}
		}

		/* Test subgroup */

		/* Send group info to all ranks */
		for (i = 0; i < opts.group_ranks.rl_nr; i++) {
			rank = opts.group_ranks.rl_ranks[i];
			if (rank != opts.self_rank)
				issue_set_grp_info(rank, 0, &grp_info);
		}
		DBG_PRINT("---------------------------------\n");

		/* Test ping each rank */
		for (i = 0; i < opts.group_ranks.rl_nr; i++) {
			/* Send to tag 0 */
			issue_test_ping(opts.group_ranks.rl_ranks[i], 0);
		}

		DBG_PRINT("---------------------------------\n");

		/* Create subgroup with 1 less member */
		DBG_PRINT("---------------------------------\n");
		DBG_PRINT("Attempting to create subgroup\n");
		rank_list->rl_nr--;
		rc = crt_group_create("my_grp", rank_list, true,
					grp_create_cb, &token_to_proceed);
		if (rc != 0) {
			DBG_PRINT("crt_group_create() failed; rc=%d\n", rc);
			assert(0);
		}
		sem_timed_wait(&token_to_proceed, 5, __LINE__);

		DBG_PRINT("Subgroup created successfully\n");
		DBG_PRINT("---------------------------------\n");
		DBG_PRINT("Attempting to lookup subgroup\n");

		tmp_grp = crt_group_lookup("my_grp");
		if (tmp_grp == NULL) {
			D_ERROR("Failed to lookup subgroup\n");
			assert(0);
		}

		if (tmp_grp != secondary_grp) {
			D_ERROR("Wrong subgroup returned\n");
			assert(0);
		}
		DBG_PRINT("Subgroup looked up successfully\n");

		DBG_PRINT("---------------------------------\n");
		DBG_PRINT("checking crt_group_rank_s2p()\n");
		for (i = 0; i < rank_list->rl_nr; i++) {
			rc = crt_group_rank_s2p(secondary_grp, i, &pri_rank);
			if (rc != 0) {
				D_ERROR("s2p failed; rc=%d\n", rc);
				assert(0);
			}

			if (pri_rank != opts.group_ranks.rl_ranks[i]) {
				D_ERROR("rank mismatch. expected %d got %d\n",
					opts.group_ranks.rl_ranks[i], pri_rank);
				assert(0);
			}


		}
		DBG_PRINT("crt_group_rank_s2p() passed on %d ranks\n",
			rank_list->rl_nr);

		/* TODO: Test P2P and CORPCs */

		/* TODO: Test removal of primary rank with secondary grp */
		DBG_PRINT("---------------------------------\n");
		DBG_PRINT("Testing crt_group_destroy()\n");
		rc = crt_group_destroy(secondary_grp, grp_destroy_cb,
				&token_to_proceed);

		sem_timed_wait(&token_to_proceed, 5, __LINE__);

		if (rc != 0) {
			D_ERROR("crt_group_destroy() failed; rc=%d\n", rc);
			assert(0);
		}

		DBG_PRINT("crt_group_destroy() PASSED\n");

		D_FREE(rank_list->rl_ranks);
		D_FREE(rank_list);

		DBG_PRINT("---------------------------------\n");
		/* Instruct rank[0] to ping all ranks */
		DBG_PRINT("Issuing indirect ping\n");
		for (x = 1; x < opts.group_ranks.rl_nr; x++) {
			issue_indirect_test_ping(opts.group_ranks.rl_ranks[0],
					0, opts.group_ranks.rl_ranks[x]);
		}

		/* Send shutdown RPC to all ranks */
		DBG_PRINT("---------------------------------\n");
		for (i = 0; i < opts.group_ranks.rl_nr; i++) {
			if (opts.group_ranks.rl_ranks[i] != opts.self_rank)
				issue_shutdown(opts.group_ranks.rl_ranks[i]);
		}
		DBG_PRINT("---------------------------------\n");

		g_do_shutdown = 1;
	} else {
		/* Each non-master rank populates uri file */
		int fd;
		char tmp_name[256];
		char full_name[256];
		char tmp_uri[256];

		DBG_PRINT("Non master rank. populating tmp file\n");
		sprintf(tmp_name, "%s%d.uri_info.tmp",
			uri_file_path, opts.self_rank);

		sprintf(full_name, "%s%d.uri_info",
			uri_file_path, opts.self_rank);

		fd = open(tmp_name, O_CREAT | O_WRONLY, 0666);
		for (i = 0; i < 2; i++) {
			rc = crt_rank_uri_get(g_group, my_rank, i, &my_uri);
			if (rc != 0) {
				D_ERROR("crt_rank_uri_get() failed; rc=%d\n",
					rc);
				assert(0);
			}

			memset(tmp_uri, 0x0, sizeof(tmp_uri));
			sprintf(tmp_uri, "%d-%d:%s\n", my_rank, i, my_uri);
			rc = write(fd, tmp_uri, strlen(tmp_uri));
			if (rc != strlen(tmp_uri)) {
				D_ERROR("Failed to write %s to file\n",
					tmp_uri);
				assert(0);
			}

		}
		close(fd);
		rename(tmp_name, full_name);

		DBG_PRINT("Generated file %s\n", full_name);
	}

	/* Wait for shutdown notification */
	while (!g_do_shutdown)
		sleep(1);

	pthread_join(progress_thread, NULL);

	DBG_PRINT("---------------------------------\n");
	DBG_PRINT("progress_thread joined. Destroying Context\n");

	rc = crt_context_destroy(aux_ctx, 1);
	if (rc != 0) {
		D_ERROR("crt_context_destroy() failed; rc=%d\n", rc);
		assert(0);
	}

	DBG_PRINT("Context destroyed. Finalizing\n");

	rc = crt_finalize();
	if (rc != 0) {
		D_ERROR("crt_finalize() failed with rc=%d\n", rc);
		assert(0);
	}

	DBG_PRINT("Finalized. Destroying semaphore\n");

	rc = sem_destroy(&token_to_proceed);
	if (rc != 0) {
		D_ERROR("sem_destroy() failed; rc=%d\n", rc);
		assert(0);
	}

	d_log_fini();

	DBG_PRINT("Destroyed semaphore. Exiting\n");
	DBG_PRINT("---------------------------------\n");
	return 0;
}

