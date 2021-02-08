/*
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * dRPC Listener Tester
 *
 * A test utility for human eyes, to sanity check the dRPC listener in the IO
 * server. Sends a bare message to the dRPC module and reports the response.
 * Requires a daos_io_server to be standing up.
 */

#include <stdio.h>
#include <unistd.h>
#include <daos/drpc.h>
#include <daos/drpc.pb-c.h>

void
print_help(char *bin_name)
{
	fprintf(stderr, "Usage: %s <socket_addr> <module> <method>\n",
			bin_name);
	fprintf(stderr, "socket_addr: path in filesystem to domain socket\n");
	fprintf(stderr, "module: numeric dRPC module ID for message\n");
	fprintf(stderr, "method: numeric dRPC method ID for message\n");
}

void
print_drpc_call(Drpc__Call *call)
{
	printf("Drpc__Call:\n");
	printf("\tSequence Number: %ld\n", call->sequence);
	printf("\tModule: %d\n", call->module);
	printf("\tMethod: %d\n", call->method);
}

const char *
get_status_string(int status)
{
	switch (status) {
	case DRPC__STATUS__SUCCESS:
		return "Success";
	case DRPC__STATUS__SUBMITTED:
		return "Submitted";
	case DRPC__STATUS__FAILURE:
		return "Failure";
	case DRPC__STATUS__UNKNOWN_MODULE:
		return "Module not recognized";
	case DRPC__STATUS__UNKNOWN_METHOD:
		return "Method not recognized";
	default:
		break;
	}

	return "Unknown status";
}

void
print_drpc_response(Drpc__Response *resp)
{
	printf("Drpc__Response:\n");
	if (resp == NULL) {
		printf("\tNULL\n");
	} else {
		printf("\tSequence Number: %ld\n", resp->sequence);
		printf("\tStatus: %s (%d)\n", get_status_string(resp->status),
				resp->status);
	}
}

int
main(int argc, char **argv)
{
	int		rc;
	struct drpc	*ctx;
	char		*socket_path;
	int		module_id;
	int		method_id;
	Drpc__Call	*call = NULL;
	Drpc__Response	*response = NULL;
	const int64_t	sequence_num = 25;

	if (argc < 4) {
		goto syntax_err;
	}

	socket_path = argv[1];

	if (sscanf(argv[2], "%d", &module_id) != 1) {
		fprintf(stderr, "Bad module ID: %s\n", argv[2]);
		goto syntax_err;
	}

	if (sscanf(argv[3], "%d", &method_id) != 1) {
		fprintf(stderr, "Bad method ID: %s\n", argv[3]);
		goto syntax_err;
	}

	rc = drpc_connect(socket_path, &ctx);
	if (rc != -DER_SUCCESS) {
		fprintf(stderr, "Bad socket path: %s\n", socket_path);
		goto syntax_err;
	}

	/* Sequence number is copied from ctx to Drpc Call under the covers */
	ctx->sequence = sequence_num;

	rc = drpc_call_create(ctx, module_id, method_id, &call);
	if (rc != DER_SUCCESS) {
		fprintf(stderr, "drpc_call_create failed: %d\n", rc);
		goto cleanup;
	}

	print_drpc_call(call);

	rc = drpc_call(ctx, R_SYNC, call, &response);
	if (rc != DER_SUCCESS) {
		fprintf(stderr, "drpc_call failed: %d\n", rc);
		goto cleanup;
	}

	printf("drpc_call() returned successfully\n");

	print_drpc_response(response);

cleanup:
	drpc_call_free(call);
	drpc_response_free(response);
	drpc_close(ctx);
	printf("Done.\n");
	return rc;

syntax_err:
	print_help(argv[0]);
	return -1;
}
