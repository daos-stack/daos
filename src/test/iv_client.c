/* Copyright (C) 2016-2018 Intel Corporation
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
 * This is a runtime test for verifying IV framework. IV Client is used for
 * initiation of tests
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include "iv_common.h"

static crt_context_t	g_crt_ctx;
static crt_endpoint_t	g_server_ep;
static char		g_hostname[100];
static bool		g_do_shutdown;

#define DBG_PRINT(x...)					\
	do {						\
		fprintf(stderr, "[%s::CLIENT]\t", g_hostname);	\
		fprintf(stderr, x);				\
	} while (0)

static void
print_usage(const char *err_msg)
{
	fprintf(stderr, "ERROR: %s\n", err_msg);

	fprintf(stderr,
		"Usage: ./iv_client -o <operation> -r <rank> [optional args]\n"
		"\n"
		"Required arguments:\n"
		"\t-o <operation> : One of ['fetch', 'update', 'invalidate', 'shutdown']\n"
		"\t-r <rank>      : Numeric rank to send the requested operation to\n"
		"\n"
		"Optional arguments:\n"
		"\t-k <key>       : Key is in form rank:key_id ; e.g. 1:0\n"
		"\t-v <value>     : Value is string, only used for update operation\n"
		"\t-s <strategy>  : One of ['none', 'eager_update', 'lazy_update', 'eager_notify', 'lazy_notify']\n"
		"\n"
		"Example usage: ./iv_client -o fetch -r 0 -k 2:9\n"
		"\tThis will initiate fetch of key [2:9] from rank 0.\n"
		"\tKey [2:9] is 9th key on rank = 2\n"
		"\tNote: Each node has 10 valid keys (0 to 9) for which that node is the root\n"
		);
}

static void
test_iv_shutdown()
{
	struct rpc_shutdown_in	*input;
	struct rpc_shutdown_out	*output;
	crt_rpc_t		*rpc_req;
	int			 rc;

	DBG_PRINT("Requesting rank %d shut down\n", g_server_ep.ep_rank);

	prepare_rpc_request(g_crt_ctx, RPC_SHUTDOWN, &g_server_ep,
			    (void **)&input, &rpc_req);

	send_rpc_request(g_crt_ctx, rpc_req, (void **)&output);

	if (output->rc == 0)
		DBG_PRINT("Shutdown of rank %d PASSED\n", g_server_ep.ep_rank);
	else
		DBG_PRINT("Shutdown of rank %d FAILED; rc = %u\n",
			  g_server_ep.ep_rank, output->rc);

	rc = crt_req_decref(rpc_req);
	assert(rc == 0);
}

static void
test_iv_invalidate(struct iv_key_struct *key)
{
	struct rpc_test_invalidate_iv_in	*input;
	struct rpc_test_invalidate_iv_out	*output;
	crt_rpc_t				*rpc_req;
	int					 rc;

	DBG_PRINT("Attempting to invalidate key[%d:%d]\n",
		  key->rank, key->key_id);

	prepare_rpc_request(g_crt_ctx, RPC_TEST_INVALIDATE_IV, &g_server_ep,
			    (void **)&input, &rpc_req);
	d_iov_set(&input->iov_key, key, sizeof(struct iv_key_struct));

	send_rpc_request(g_crt_ctx, rpc_req, (void **)&output);

	if (output->rc == 0)
		DBG_PRINT("Invalidate of key=[%d:%d] PASSED\n", key->rank,
			  key->key_id);
	else
		DBG_PRINT("Invalidate of key=[%d:%d] FAILED; rc = %ld\n",
			  key->rank, key->key_id, output->rc);

	rc = crt_req_decref(rpc_req);
	assert(rc == 0);
}

/** Prints a buffer as hex without any newlines/spaces/etc */
static void
print_hex(void *buf, size_t len)
{
	uint8_t *bytes = (uint8_t *)buf;

	if (bytes == NULL)
		return;

	for (; len > 0; len--) {
		printf("%02X", *bytes);
		bytes++;
	}
}

/**
 * Prints a scatter-gather list as components of a JSON array in hex like:
 *
 * <prefix>"DEADBEEF",
 * <prefix>"FEEDCODE"
 *
 * Prefix should probably be something like "\t\t" for pretty formatting
 */
static void print_sgl_as_json(d_sg_list_t *sg_list, char *prefix)
{
	uint32_t i;

	for (i = 0; i < sg_list->sg_nr.num; i++) {
		printf("%s\"", prefix);
		print_hex(sg_list->sg_iovs[i].iov_buf,
			  sg_list->sg_iovs[i].iov_buf_len);
		printf("\"");
		if (i + 1 < sg_list->sg_nr.num)
			printf(",");
		printf("\n");
	}
}

/**
 * Print the result of a fetch as valid JSON
 *
 * This isn't very extensible - would probably need a real
 * JSON library to generalize this
 */
static void print_result_as_json(int64_t return_code, d_iov_t *key,
				 d_sg_list_t *sg_list)
{
	printf("{\n");
	printf("\t\"return_code\":\"%ld\",\n", return_code);
	printf("\t\"key\":\"");
	print_hex(key->iov_buf, key->iov_len);
	printf("\",\n");
	printf("\t\"value\":[\n");
	print_sgl_as_json(sg_list, "\t\t");
	printf("\t]\n");
	printf("}\n");
}

/**
 * This function initiates a fetch on the specified node for the specified key
 * index. If that succeeds, the node sends back the results of the fetch
 * using BULK_PUT
 */
static void
test_iv_fetch(struct iv_key_struct *key)
{
	struct rpc_test_fetch_iv_in	*input;
	struct rpc_test_fetch_iv_out	*output;
	crt_rpc_t			*rpc_req;
	uint8_t				*buf = NULL;
	d_sg_list_t			 sg_list;
	crt_bulk_perm_t			 perms = CRT_BULK_RW;
	int				 rc;

	DBG_PRINT("Attempting fetch for key[%d:%d]\n", key->rank, key->key_id);

	prepare_rpc_request(g_crt_ctx, RPC_TEST_FETCH_IV, &g_server_ep,
			    (void **)&input, &rpc_req);

	/* Create a temporary buffer to store the result of the fetch */
	D_ALLOC(buf, MAX_DATA_SIZE);
	assert(buf != NULL);
	rc = d_sgl_init(&sg_list, 1);
	assert(rc == 0);
	d_iov_set(&sg_list.sg_iovs[0], buf, MAX_DATA_SIZE);

	/* Create a local handle to be used to BULK_PUT the fetch result */
	rc = crt_bulk_create(g_crt_ctx, &sg_list, perms, &input->bulk_hdl);
	assert(rc == 0);
	D_ASSERT(input->bulk_hdl != NULL);

	d_iov_set(&input->key, key, sizeof(struct iv_key_struct));

	/* Send the FETCH request to the test server */
	send_rpc_request(g_crt_ctx, rpc_req, (void **)&output);

	if (output->rc == 0)
		DBG_PRINT("Fetch of key=[%d:%d] PASSED\n", key->rank,
			  key->key_id);
	else
		DBG_PRINT("Fetch of key=[%d:%d] FAILED; rc = %ld\n", key->rank,
			  key->key_id, output->rc);

	print_result_as_json(output->rc, &output->key, &sg_list);

	/* Cleanup */
	rc = crt_req_decref(rpc_req);
	assert(rc == 0);

	rc = crt_bulk_free(input->bulk_hdl);
	assert(rc == 0);

	/* Frees the IOV buf also */
	d_sgl_fini(&sg_list, true);
}

static int
test_iv_update(struct iv_key_struct *key, char *str_value, char *arg_sync)
{
	struct rpc_test_update_iv_in	*input;
	struct rpc_test_update_iv_out	*output;
	crt_rpc_t			*rpc_req;
	int				 rc;
	crt_iv_sync_t			 sync;

	if (arg_sync == NULL) {
		sync.ivs_mode = 0;
		sync.ivs_event = 0;
	} else if (strcmp(arg_sync, "none") == 0) {
		sync.ivs_mode = 0;
		sync.ivs_event = 0;
	} else if (strcmp(arg_sync, "eager_update") == 0) {
		sync.ivs_mode = CRT_IV_SYNC_EAGER;
		sync.ivs_event = CRT_IV_SYNC_EVENT_UPDATE;
	} else if (strcmp(arg_sync, "lazy_update") == 0) {
		sync.ivs_mode = CRT_IV_SYNC_LAZY;
		sync.ivs_event = CRT_IV_SYNC_EVENT_UPDATE;
	} else if (strcmp(arg_sync, "eager_notify") == 0) {
		sync.ivs_mode = CRT_IV_SYNC_EAGER;
		sync.ivs_event = CRT_IV_SYNC_EVENT_NOTIFY;
	} else if (strcmp(arg_sync, "eager_update") == 0) {
		sync.ivs_mode = CRT_IV_SYNC_EAGER;
		sync.ivs_event = CRT_IV_SYNC_EVENT_UPDATE;
	} else {
		print_usage("Unknown sync option specified");
		return -1;
	}

	prepare_rpc_request(g_crt_ctx, RPC_TEST_UPDATE_IV, &g_server_ep,
			    (void **)&input, &rpc_req);
	d_iov_set(&input->iov_key, key, sizeof(struct iv_key_struct));
	d_iov_set(&input->iov_sync, &sync, sizeof(crt_iv_sync_t));
	input->str_value = str_value;

	send_rpc_request(g_crt_ctx, rpc_req, (void **)&output);

	if (output->rc == 0)
		DBG_PRINT("Update PASSED\n");
	else
		DBG_PRINT("Update FAILED; rc = %ld\n", output->rc);

	rc = crt_req_decref(rpc_req);
	assert(rc == 0);

	return 0;
}

enum op_type {
	OP_FETCH,
	OP_UPDATE,
	OP_INVALIDATE,
	OP_SHUTDOWN,
	OP_NONE,
};

static void *
progress_function(void *data)
{
	crt_context_t *p_ctx = (crt_context_t *)data;

	while (g_do_shutdown == 0)
		crt_progress(*p_ctx, 1000, NULL, NULL);

	crt_context_destroy(*p_ctx, 1);

	return NULL;
}

int main(int argc, char **argv)
{
	struct iv_key_struct	 iv_key;
	crt_group_t		*srv_grp;
	char			*arg_rank = NULL;
	char			*arg_op = NULL;
	char			*arg_key = NULL;
	char			*arg_value = NULL;
	char			*arg_sync = NULL;
	enum op_type		 cur_op = OP_NONE;
	int			 rc = 0;
	pthread_t		 progress_thread;
	int			 c;

	init_hostname(g_hostname, sizeof(g_hostname));

	while ((c = getopt(argc, argv, "k:o:r:s:v:")) != -1) {
		switch (c) {
		case 'r':
			arg_rank = optarg;
			break;
		case 'o':
			arg_op = optarg;
			break;
		case 'k':
			arg_key = optarg;
			break;
		case 'v':
			arg_value = optarg;
			break;
		case 's':
			arg_sync = optarg;
			break;
		default:
			fprintf(stderr, "Unknown option %d\n", c);
			print_usage("Bad option");
			return -1;
		}
	}

	if (arg_rank == NULL || arg_op == NULL) {
		print_usage("Rank (-r) and Operation (-o) must be specified");
		return -1;
	}

	if (strcmp(arg_op, "fetch") == 0) {
		if (arg_value != NULL) {
			print_usage("Value shouldn't be supplied for fetch");
			return -1;
		}
		cur_op = OP_FETCH;
	} else if (strcmp(arg_op, "update") == 0) {
		cur_op = OP_UPDATE;

		if (arg_value == NULL) {
			print_usage("Value must be specifie for the update");
			return -1;
		}
	} else if (strcmp(arg_op, "invalidate") == 0) {
		cur_op = OP_INVALIDATE;
	} else if (strcmp(arg_op, "shutdown") == 0) {
		if (arg_key != NULL) {
			print_usage("Key shouldn't be supplied for shutdown");
			return -1;
		}
		cur_op = OP_SHUTDOWN;
	} else {
		print_usage("Unknown operation");
		return -1;
	}

	if (arg_key == NULL && cur_op != OP_SHUTDOWN) {
		print_usage("Key (-k) is required for this operation");
		return -1;
	}

	rc = crt_init(NULL, CRT_FLAG_BIT_SINGLETON);
	assert(rc == 0);

	rc = crt_context_create(&g_crt_ctx);
	assert(rc == 0);

	rc = crt_group_attach(CRT_DEFAULT_SRV_GRPID, &srv_grp);
	assert(rc == 0);

	rc = pthread_create(&progress_thread, 0, progress_function, &g_crt_ctx);
	assert(rc == 0);

	rc = RPC_REGISTER(RPC_TEST_FETCH_IV);
	assert(rc == 0);

	rc = RPC_REGISTER(RPC_TEST_UPDATE_IV);
	assert(rc == 0);

	rc = RPC_REGISTER(RPC_TEST_INVALIDATE_IV);
	assert(rc == 0);

	rc = RPC_REGISTER(RPC_SHUTDOWN);
	assert(rc == 0);

	g_server_ep.ep_grp = srv_grp;
	g_server_ep.ep_rank = atoi(arg_rank);
	g_server_ep.ep_tag = 0;

	if (arg_key != NULL
	    && sscanf(arg_key, "%d:%d", &iv_key.rank, &iv_key.key_id) != 2) {
		print_usage("Bad key format, should be rank:id");
		return -1;
	}

	if (cur_op == OP_FETCH)
		test_iv_fetch(&iv_key);
	else if (cur_op == OP_UPDATE)
		test_iv_update(&iv_key, arg_value, arg_sync);
	else if (cur_op == OP_INVALIDATE)
		test_iv_invalidate(&iv_key);
	else if (cur_op == OP_SHUTDOWN)
		test_iv_shutdown();

	else {
		print_usage("Unsupported opration");
		return -1;
	}

	g_do_shutdown = true;
	pthread_join(progress_thread, NULL);

	DBG_PRINT("Exiting client\n");

	return rc;
}
