/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "../transport.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* A single RC QP connected to itself exercises the real HCA without a
 * bootstrap service. This is a transport smoke test, not a CaRT RPC test.
 * Run as an unprivileged user with sufficient locked-memory allowance.
 */
static int
wait_for(struct crt_native_ep *ep, unsigned wanted)
{
	const struct crt_native_ops *ops = &crt_native_verbs_ops;
	struct crt_native_completion out[8];
	struct timespec              start, now;
	unsigned                     done = 0;

	clock_gettime(CLOCK_MONOTONIC, &start);
	while (done < wanted) {
		unsigned count = 0;
		int      rc    = ops->progress(ep, out, 8, &count);

		for (unsigned i = 0; i < count; i++) {
			if (out[i].error != 0)
				return out[i].error;
		}
		if (rc != 0)
			return rc;
		done += count;
		clock_gettime(CLOCK_MONOTONIC, &now);
		if (now.tv_sec - start.tv_sec > 10)
			return ETIMEDOUT;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	const struct crt_native_ops    *ops    = &crt_native_verbs_ops;
	struct crt_native_config        config = {.port = 1, .gid_index = -1, .depth = 16};
	struct crt_native_address       address;
	struct crt_native_remote_memory remote;
	struct crt_native_ep           *ep;
	struct crt_native_mr           *mr;
	char                            buffer[256] = "native CaRT transport";
	int                             rc;

	if (argc < 2 || argc > 4) {
		fprintf(stderr, "Usage: %s DEVICE [PORT [GID_INDEX]]\n", argv[0]);
		return 2;
	}
	config.device = argv[1];
	if (argc > 2) {
		char *end;
		long  port = strtol(argv[2], &end, 10);

		if (*end != '\0' || port < 1 || port > 255)
			return 2;
		config.port = (uint8_t)port;
	}
	if (argc > 3) {
		char *end;
		long  gid = strtol(argv[3], &end, 10);

		if (*end != '\0' || gid < -1 || gid > 255)
			return 2;
		config.gid_index = (int)gid;
	}

/* On failure terminate the process without reusing buffers: the kernel tears
 * down its RDMA resources. A reusable library caller must instead flush/drain.
 */
#define CHECK(call)                                                                                \
	do {                                                                                       \
		rc = (call);                                                                       \
		if (rc != 0) {                                                                     \
			fprintf(stderr, "%s failed: %s (%d)\n", #call, strerror(rc), rc);          \
			exit(1);                                                                   \
		}                                                                                  \
	} while (0)
	CHECK(ops->open(&config, &ep));
	CHECK(ops->address(ep, &address));
	CHECK(ops->register_memory(ep, buffer, sizeof(buffer),
				   CRT_NATIVE_REMOTE_READ | CRT_NATIVE_REMOTE_WRITE, &mr, &remote));
	CHECK(ops->post(ep, CRT_NATIVE_RECV, mr, 64, 32, NULL, 0, NULL));
	CHECK(ops->connect(ep, &address));
	CHECK(ops->post(ep, CRT_NATIVE_SEND, mr, 0, 32, NULL, 0, NULL));
	CHECK(wait_for(ep, 2));
	if (memcmp(buffer, buffer + 64, 32) != 0)
		return 1;
	CHECK(ops->post(ep, CRT_NATIVE_WRITE, mr, 0, 32, &remote, 128, NULL));
	CHECK(wait_for(ep, 1));
	CHECK(ops->post(ep, CRT_NATIVE_READ, mr, 192, 32, &remote, 128, NULL));
	CHECK(wait_for(ep, 1));
	if (memcmp(buffer, buffer + 192, 32) != 0)
		return 1;
	CHECK(ops->close_begin(ep));
	CHECK(ops->deregister_memory(mr));
	CHECK(ops->close(ep));
	puts("real verbs SEND/RECV and RDMA WRITE/READ smoke test passed");
	return 0;
}
