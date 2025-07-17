/**
 * (C) Copyright 2023-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_DRPC_TYPES_H__
#define __DAOS_DRPC_TYPES_H__

#include <daos/drpc.pb-c.h>

/*
 * Using a packetsocket over the unix domain socket means that we receive
 * a whole message at a time without knowing its size. So for this reason
 * we need to restrict the maximum message size so we can preallocate a
 * buffer to put all of the information in.
 */
#define UNIXCOMM_MAXMSGSIZE (1 << 17)

struct unixcomm {
	int fd;    /** File descriptor of the unix domain socket */
	int flags; /** Flags set on unix domain socket */
};

typedef void (*drpc_handler_t)(Drpc__Call *, Drpc__Response *);
typedef int (*drpc_thread_yielder_t)(void);

/**
 * dRPC connection context. This includes all details needed to communicate
 * on the dRPC channel.
 */
struct drpc {
	struct unixcomm      *comm;      /** unix domain socket communication context */
	int                   sequence;  /** sequence number of latest message sent */
	uint32_t              ref_count; /** open refs to this ctx */

	/**
	 * Handler for messages received by a listening drpc context.
	 * For client contexts, this is NULL.
	 */
	drpc_handler_t        handler;

	/**
	 * Function to yield cycles to a non-dRPC thread, if applicable.
	 */
	drpc_thread_yielder_t yield;
};

enum rpcflags { R_SYNC = 1 };

/**
 * drpc_header precedes every dRPC message chunk.
 */
struct drpc_header {
	size_t   total_data_size;
	size_t   chunk_data_size;
	uint32_t chunk_idx;
	uint32_t total_chunks;
};

#endif /* __DAOS_DRPC_TYPES_H__ */
