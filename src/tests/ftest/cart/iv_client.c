/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This is a runtime test for verifying IV framework. IV Client is used for
 * initiation of tests
 */

/*
 * TODO:
 * Jan 23, 2018 - Byron Marohn
 *
 * A few things here could use improvement.
 * - Shutdown sometimes hangs, forcing the caller to forcibly kill the process
 * - Keys are currently passed all the way back to the caller,
 *   which is probably unnecessary.
 * - The scatter-gather list used to transfer IV values back and forth is
 *   fixed to using just the first IOV buffer, which doesn't cover all the
 *   potential use cases for IV
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include <errno.h>
#include "iv_common.h"

static crt_context_t	g_crt_ctx;
static crt_endpoint_t	g_server_ep;
static bool		g_do_shutdown;

static void
print_usage(const char *err_msg)
{
	fprintf(stderr, "ERROR: %s\n", err_msg);

	fprintf(stderr,
		"Usage: ./iv_client -o <operation> -r <rank> [optional args]\n"
		"\n"
		"Required arguments:\n"
		"\t-o <operation> : One of ['fetch', 'update', 'invalidate', 'shutdown'\n"
		"			    'get_grp_version', 'set_grp_version']\n"
		"\t-r <rank>      : Numeric rank to send the requested operation to\n"
		"\n"
		"Optional arguments:\n"
		"\t-k <key>       : Key is in form rank:key_id ; e.g. 1:0\n"
		"\t-v <value>     : Value is string, only used for update operation\n"
		"\t-x <value>     : Value as hex string, only used for update operation\n"
		"\t-s <strategy>  : One of ['none', 'eager_update', 'lazy_update', 'eager_notify', 'lazy_notify']\n"
		"\t-l <log.txt>   : Print results to log file instead of stdout\n"
		"\t-m <value>     : Value as string, used to control timing to change group version\n"
		"\t		 0  - change at time of call.\n"
		"\t		 1  - change at end of iv_test_fetch.\n"
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
	struct RPC_SHUTDOWN_in	*input;
	struct RPC_SHUTDOWN_out	*output;
	crt_rpc_t               *rpc_req;

	DBG_PRINT("Requesting rank %d shut down\n", g_server_ep.ep_rank);

	prepare_rpc_request(g_crt_ctx, RPC_SHUTDOWN, &g_server_ep,
			    (void **)&input, &rpc_req);

	send_rpc_request(g_crt_ctx, rpc_req, (void **)&output);

	if (output->rc == 0)
		DBG_PRINT("Shutdown of rank %d PASSED\n", g_server_ep.ep_rank);
	else
		DBG_PRINT("Shutdown of rank %d FAILED; rc = %u\n",
			  g_server_ep.ep_rank, output->rc);

	crt_req_decref(rpc_req);
}

static int
create_sync(char *arg_sync, crt_iv_sync_t **rsync)
{
	crt_iv_sync_t				*sync;

	D_ALLOC_PTR(sync);
	assert(sync != NULL);

	if (arg_sync == NULL) {
		sync->ivs_mode = 0;
		sync->ivs_event = 0;
	} else if (strcmp(arg_sync, "none") == 0) {
		sync->ivs_mode = 0;
		sync->ivs_event = 0;
	} else if (strcmp(arg_sync, "eager_update") == 0) {
		sync->ivs_mode = CRT_IV_SYNC_EAGER;
		sync->ivs_event = CRT_IV_SYNC_EVENT_UPDATE;
	} else if (strcmp(arg_sync, "lazy_update") == 0) {
		sync->ivs_mode = CRT_IV_SYNC_LAZY;
		sync->ivs_event = CRT_IV_SYNC_EVENT_UPDATE;

	} else if (strcmp(arg_sync, "eager_notify") == 0) {
		sync->ivs_mode = CRT_IV_SYNC_EAGER;
		sync->ivs_event = CRT_IV_SYNC_EVENT_NOTIFY;
	} else if (strcmp(arg_sync, "lazy_notify") == 0) {
		sync->ivs_mode = CRT_IV_SYNC_LAZY;
		sync->ivs_event = CRT_IV_SYNC_EVENT_NOTIFY;
	} else {
		print_usage("Unknown sync option specified");
		D_FREE(sync);
		return -1;
	}

	*rsync  = sync;
	return 0;
}

static void
test_iv_invalidate(struct iv_key_struct *key, char *arg_sync)
{
	struct RPC_TEST_INVALIDATE_IV_in	*input;
	struct RPC_TEST_INVALIDATE_IV_out	*output;
	crt_rpc_t				*rpc_req;
	int					 rc;
	crt_iv_sync_t				*sync = NULL;

	DBG_PRINT("Attempting to invalidate key[%d:%d]: sync type: %s\n",
		  key->rank, key->key_id, arg_sync);

	rc = create_sync(arg_sync, &sync);
	if (rc != 0) {
		/* Avoid checkpatch warning */
		goto exit_code;
	}

	prepare_rpc_request(g_crt_ctx, RPC_TEST_INVALIDATE_IV, &g_server_ep,
			    (void **)&input, &rpc_req);

	/* Copy parameters into rpc input structure */
	d_iov_set(&input->iov_key, key, sizeof(struct iv_key_struct));
	d_iov_set_safe(&input->iov_sync, sync, sizeof(crt_iv_sync_t));

	send_rpc_request(g_crt_ctx, rpc_req, (void **)&output);

	if (output->rc == 0)
		DBG_PRINT("Invalidate of key=[%d:%d] PASSED\n", key->rank,
			  key->key_id);
	else
		DBG_PRINT("Invalidate of key=[%d:%d] FAILED; rc = %d\n",
			  key->rank, key->key_id, output->rc);

	crt_req_decref(rpc_req);
	D_FREE(sync);
exit_code:
	assert(rc == 0);
}

/**
 * Takes a single hex character (two ascii digits and a null character) and
 * parses it as hex, returning the resulting byte into *res
 *
 * Checks that input values are valid and works with lowercase/capital letters.
 *
 * \return	0 on success, nonzero on error
 */
static int
unpack_hex_byte(char hex[3], char *res) {
	int x;

	for (x = 0; x < 2; x++) {
		/* Convert to lower case */
		if (hex[x] >= 'A' && hex[x] <= 'F')
			hex[x] = hex[x] + ('a' - 'A');

		/* Look for invalid characters */
		if ((hex[x] < 'a' || hex[x] > 'f') &&
		    (hex[x] < '0' || hex[x] > '9'))
			return 1;
	}

	x = sscanf(hex, "%2hhx", res);
	if (x != 1)
		return 1;

	return 0;
}

/**
 * Takes a string of hex characters and converts it to a byte buffer of those
 * specified bytes. The resulting byte array occupies the first half of the
 * given supplied string, overwriting the ASCII hex 0-9a-f characters in the
 * first half.
 *
 * len is the output length of the byte array, which is always strlen(str) / 2;
 *
 * \return	0 on success, nonzero on error
 */
static int
unpack_hex_string_inplace(char *str, size_t *len)
{
	size_t slen = strlen(str);
	size_t offset = 0;
	int rc;

	*len = 0;

	if (slen % 2 != 0) {
		/* If the string is odd-length, add a leading 0 */
		char temp[3] = {'0', str[0], '\0'};

		rc = unpack_hex_byte(temp, &str[0]);
		if (rc)
			return rc;

		/* Future math to find hex coordinates is now off by 1 */
		offset = -1;

		*len = *len + 1;
		slen -= 1;
	}

	for (; slen > 0; slen -= 2) {
		char temp[3] = {str[(*len) * 2 + offset],
				str[(*len) * 2 + 1 + offset],
				'\0'};

		rc = unpack_hex_byte(temp, &str[*len]);
		if (rc)
			return rc;

		*len = *len + 1;
	}

	return 0;
}

/**
 * Print the result of a fetch as valid JSON
 *
 * This isn't very extensible - would probably need a real
 * JSON library to generalize this
 *
 * Only the first 'size' bytes of the first IOV in the sg_list is printed as hex
 */
static void print_result_as_json(int64_t return_code, d_iov_t *key,
				 uint64_t size, d_sg_list_t *sg_list,
				 FILE *log_file)
{
	assert(sg_list->sg_nr == 1);
	assert(sg_list->sg_iovs[0].iov_buf_len >= size);

	fprintf(log_file, "{\n");
	fprintf(log_file, "\t\"return_code\":%ld,\n", return_code);
	fprintf(log_file, "\t\"key\":\"");
	print_hex(key->iov_buf, key->iov_len, log_file);
	fprintf(log_file, "\",\n");
	fprintf(log_file, "\t\"value\":\"");
	print_hex(sg_list->sg_iovs[0].iov_buf, size, log_file);
	fprintf(log_file, "\"\n");
	fprintf(log_file, "}\n");
	fflush(log_file);
}

/**
 * This function initiates a fetch on the specified node for the specified key
 * index. If that succeeds, the node sends back the results of the fetch
 * using BULK_PUT
 */
static void
test_iv_fetch(struct iv_key_struct *key, FILE *log_file)
{
	struct RPC_TEST_FETCH_IV_in	*input;
	struct RPC_TEST_FETCH_IV_out	*output;
	crt_rpc_t			*rpc_req = NULL;
	uint8_t				*buf = NULL;
	d_sg_list_t			 sg_list;
	crt_bulk_perm_t			 perms = CRT_BULK_RW;
	int				 rc;

	DBG_PRINT("Attempting fetch for key[%d:%d]\n", key->rank, key->key_id);

	rc = prepare_rpc_request(g_crt_ctx, RPC_TEST_FETCH_IV, &g_server_ep,
				 (void **)&input, &rpc_req);
	assert(rc == 0);

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
		DBG_PRINT("Fetch of key=[%d:%d] FOUND\n", key->rank,
			  key->key_id);
	else
		DBG_PRINT("Fetch of key=[%d:%d] NOT FOUND; rc = %ld\n",
			  key->rank, key->key_id, output->rc);

	print_result_as_json(output->rc, &output->key, output->size, &sg_list,
			     log_file);

	/* Cleanup */
	rc = crt_bulk_free(input->bulk_hdl);
	assert(rc == 0);

	crt_req_decref(rpc_req);

	/* Frees the IOV buf also */
	d_sgl_fini(&sg_list, true);
}

/* Modify iv synchronization type and search tree */
static int
test_iv_update(struct iv_key_struct *key, char *str_value, bool value_is_hex,
	       char *arg_sync)
{
	struct RPC_TEST_UPDATE_IV_in  *input;
	struct RPC_TEST_UPDATE_IV_out *output;
	crt_rpc_t                     *rpc_req;
	size_t                         len;
	int                            rc;
	crt_iv_sync_t                 *sync = NULL;

	rc = create_sync(arg_sync, &sync);
	if (rc != 0) {
		/* Avoid checkpatch warning */
		goto exit_code;
	}

	prepare_rpc_request(g_crt_ctx, RPC_TEST_UPDATE_IV, &g_server_ep,
			    (void **)&input, &rpc_req);
	d_iov_set(&input->iov_key, key, sizeof(struct iv_key_struct));
	d_iov_set_safe(&input->iov_sync, sync, sizeof(crt_iv_sync_t));

	if (value_is_hex) {
		rc = unpack_hex_string_inplace(str_value, &len);
		if (rc) {
			fprintf(stderr, "Failed to parse supplied hex value\n");
			return rc;
		}
		d_iov_set(&input->iov_value, str_value, len);
	} else {
		d_iov_set(&input->iov_value, str_value, strlen(str_value) + 1);
	}

	send_rpc_request(g_crt_ctx, rpc_req, (void **)&output);

	D_FREE(sync);

	if (output->rc == 0)
		DBG_PRINT("Update PASSED\n");
	else
		DBG_PRINT("Update FAILED; rc = %ld\n", output->rc);

	crt_req_decref(rpc_req);
exit_code:
	assert(rc == 0);
	return 0;
}

/*
 * The argument arg_timing allows for the caller to specify
 * when a change in the group version number occurs.
 * Under normal situations,this value should be zero, which
 * indicates that the version change should occur at the time
 * of the call.
 * However, if this is not zero, then it allows for the change
 * in version number to occur at some other time, implementer
 * discression.  Its intention is to allow the change in version
 * number within a call back function; thus simulating an
 * asynchronis event requesting a version change while the
 * system is handling another iv request.
 * Currently, there are 2 time out values implemented:
 *    Value    CallBack          Test
 *      1    iv_test_fetch_iv   Change in version after call to
 *                              crt_iv_fetch
 *      2    iv_pre_fetch       Change in version while in function
 *                              crt_hdlr_if_fetch_aux
 */
static int
test_iv_set_grp_version(char *arg_version, char *arg_timing)
{
	struct RPC_SET_GRP_VERSION_in		*input;
	struct RPC_SET_GRP_VERSION_out		*output;
	crt_rpc_t                               *rpc_req;
	int					 version;
	int					 time = 0;
	char					*tmp;

	/* See if string contains valid hex characters.  If so assume
	 * base 16.  Else, default to base 10.
	 */
	tmp = strpbrk(arg_version, "abcdABCDxX");
	if (tmp == NULL)
		version = strtol(arg_version, NULL, 10);
	else
		version = strtol(arg_version, NULL, 16);

	DBG_PRINT("Attempting to set group version to 0x%08x: %d\n",
		  version,  version);

	/* decode timing for changing version */
	if (arg_timing != NULL) {
		/* Avoid check patch warning */
		time = strtol(arg_timing, NULL, 10);
	}

	prepare_rpc_request(g_crt_ctx, RPC_SET_GRP_VERSION, &g_server_ep,
			    (void **)&input, &rpc_req);

	/* Fill in the input structure */
	input->version = version;
	input->timing = time;

	/* send the request */
	send_rpc_request(g_crt_ctx, rpc_req, (void **)&output);

	/* Check of valid output */
	if (output->rc == 0)
		DBG_PRINT("Grp Set Version PASSED 0x%x : %d\n",
			  version, version);
	else
		DBG_PRINT("Grp Set Version FAILED 0x%x : %d\n",
			  version, version);

	crt_req_decref(rpc_req);

	return 0;
}

static int
test_iv_get_grp_version()
{
	struct RPC_GET_GRP_VERSION_in  *input;
	struct RPC_GET_GRP_VERSION_out *output;
	crt_rpc_t                      *rpc_req;
	int                             version = 0;

	prepare_rpc_request(g_crt_ctx, RPC_GET_GRP_VERSION, &g_server_ep,
			    (void **)&input, &rpc_req);

	DBG_PRINT("Attempting to get group version\n");
	send_rpc_request(g_crt_ctx, rpc_req, (void **)&output);

	/* Check for valid output */
	version = output->version;
	if (output->rc != 0)
		DBG_PRINT("Grp Get Version FAILED: rc %d\n",
			  output->rc);
	else
		DBG_PRINT("Grp Get Version PASSED 0x%08x : %d\n",
			  version, version);

	crt_req_decref(rpc_req);
	return 0;
}

enum op_type {
	OP_FETCH,
	OP_UPDATE,
	OP_INVALIDATE,
	OP_SHUTDOWN,
	OP_SET_GRP_VERSION,
	OP_GET_GRP_VERSION,
	OP_NONE,
};

static void *
progress_function(void *data)
{
	crt_context_t *p_ctx = (crt_context_t *)data;

	while (g_do_shutdown == 0)
		crt_progress(*p_ctx, 1000);

	crt_context_destroy(*p_ctx, 1);

	return NULL;
}

#define NUM_ATTACH_RETRIES 30

int main(int argc, char **argv)
{
	struct iv_key_struct	 iv_key;
	crt_group_t		*srv_grp;
	char			*arg_rank = NULL;
	char			*arg_op = NULL;
	char			*arg_key = NULL;
	char			*arg_value = NULL;
	bool			 arg_value_is_hex = false;
	char			*arg_sync = NULL;
	char			*arg_log = NULL;
	char			*arg_time = NULL;
	FILE			*log_file = stdout;
	enum op_type		 cur_op = OP_NONE;
	int			 rc = 0;
	pthread_t		 progress_thread;
	int			 c;
	int			 attach_retries_left;

	DBG_PRINT("\t*******************\n");
	DBG_PRINT("\t***Client MAIN ****\n");
	DBG_PRINT("\t*******************\n");

	while ((c = getopt(argc, argv, "k:o:r:s:v:x:l:m:")) != -1) {
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
			arg_value_is_hex = false;
			break;
		case 'x':
			arg_value = optarg;
			arg_value_is_hex = true;
			break;
		case 's':
			arg_sync = optarg;
			break;
		case 'l':
			arg_log = optarg;
			break;
		case 'm':
			arg_time = optarg;
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
			print_usage("Value must be supplied for update");
			return -1;
		}
	} else if (strcmp(arg_op, "invalidate") == 0) {
		/* Avoid check patch warning */
		cur_op = OP_INVALIDATE;
	} else if (strcmp(arg_op, "shutdown") == 0) {
		if (arg_key != NULL) {
			print_usage("Key shouldn't be supplied for shutdown");
			return -1;
		}
		cur_op = OP_SHUTDOWN;
	} else if (strcmp(arg_op, "set_grp_version") == 0) {
		if (arg_value == 0) {
			print_usage("Version must be supplied");
			return -1;
		}
		cur_op = OP_SET_GRP_VERSION;
	} else if (strcmp(arg_op, "get_grp_version") == 0) {
		cur_op = OP_GET_GRP_VERSION;

	} else {
		print_usage("Unknown operation");
		return -1;
	}

	if (arg_key == NULL && !((cur_op == OP_SHUTDOWN) ||
				 (cur_op == OP_SET_GRP_VERSION) ||
				 (cur_op == OP_GET_GRP_VERSION))) {
		print_usage("Key (-k) is required for this operation");
		return -1;
	}

	if (arg_log != NULL) {
		/* Overwrite file, don't append */
		log_file = fopen(arg_log, "w");
		if (log_file == NULL) {
			printf("Error opening file '%s': %s (%d)\n", arg_log,
			       strerror(errno), errno);
			return -1;
		}
	}

	/* rank, num_attach_retries, is_server, assert_on_error */
	crtu_test_init(0, 20, false, true);

	rc = crt_init(IV_GRP_NAME, 0);
	assert(rc == 0);

	rc = crt_context_create(&g_crt_ctx);
	assert(rc == 0);

	attach_retries_left = NUM_ATTACH_RETRIES;

	while (attach_retries_left-- > 0) {
		rc = crt_group_attach(IV_GRP_NAME, &srv_grp);
		if (rc == 0)
			break;

		printf("attach failed (rc=%d). retries left %d\n",
		       rc, attach_retries_left);
		sleep(1);
	}
	assert(rc == 0);

	rc = pthread_create(&progress_thread, 0, progress_function, &g_crt_ctx);
	assert(rc == 0);

	rc = crt_proto_register(&my_proto_fmt_iv);
	assert(rc == 0);

	g_server_ep.ep_grp = srv_grp;
	g_server_ep.ep_rank = atoi(arg_rank);
	g_server_ep.ep_tag = 0;

	if (arg_key != NULL &&
	    sscanf(arg_key, "%d:%d", &iv_key.rank, &iv_key.key_id) != 2) {
		print_usage("Bad key format, should be rank:id");
		return -1;
	}

	if (cur_op == OP_FETCH) {
		test_iv_fetch(&iv_key, log_file);
	} else if (cur_op == OP_UPDATE) {
		test_iv_update(&iv_key, arg_value, arg_value_is_hex, arg_sync);
	} else if (cur_op == OP_INVALIDATE) {
		test_iv_invalidate(&iv_key, arg_sync);
	} else if (cur_op == OP_SHUTDOWN) {
		test_iv_shutdown();
	} else if (cur_op == OP_SET_GRP_VERSION) {
		test_iv_set_grp_version(arg_value, arg_time);
	} else if (cur_op == OP_GET_GRP_VERSION) {
		test_iv_get_grp_version();
	} else {
		print_usage("Unsupported operation");
		return -1;
	}

	crt_group_detach(srv_grp);

	g_do_shutdown = true;
	pthread_join(progress_thread, NULL);

	DBG_PRINT("Exiting client\n");

	if (log_file != stdout)
		fclose(log_file);

	crt_finalize();
	return rc;
}
