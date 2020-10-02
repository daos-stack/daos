/*
 * (C) Copyright 2018-2020 Intel Corporation.
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <daos/drpc.h>
#include "drpc_test.pb-c.h"

void
print_usage(void)
{
	fprintf(stderr, "Usage: daos_test <socket_addr> \"<message>\"\n");
	fprintf(stderr, "socket_addr: path in file system to domain socket\n");
	fprintf(stderr, "message: text to send (must be in quotes)\n");
}

int
main(int argc, char **argv)
{
	struct drpc		*ctx = NULL;
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		*response = NULL;
	Hello__Hello		body = HELLO__HELLO__INIT;
	Hello__HelloResponse	*hResponse = NULL;
	uint8_t			*body_buffer;
	int			body_buffer_length;
	int			ret;

	if (argc < 3) {
		print_usage();
		exit(1);
	}

	ret = drpc_connect(argv[1], &ctx);
	if (ret != -DER_SUCCESS) {
		fprintf(stderr, "Unable to connect to %s\n", argv[1]);
		exit(1);
	}

	body.name = argv[2];

	body_buffer_length = hello__hello__get_packed_size(&body);
	D_ALLOC(body_buffer, body_buffer_length);
	if (!body_buffer) {
		fprintf(stderr, "Unable to allocate buffer for call body\n");
		exit(1);
	}
	hello__hello__pack(&body, body_buffer);

	call.module = HELLO__MODULE__HELLO;
	call.method = HELLO__FUNCTION__GREETING;
	call.body.data = body_buffer;
	call.body.len = body_buffer_length;

	ret = drpc_call(ctx, R_SYNC, &call, &response);
	drpc_close(ctx);
	if (ret < 0) {
		fprintf(stderr, "drpc_call failed: %d", ret);
		exit(1);
	}

	hResponse = hello__hello_response__unpack(NULL,
							response->body.len,
							response->body.data);
	fprintf(stdout, "Response message: %s\n", hResponse->greeting);
	return 0;
}
