/**
 * (C) Copyright 2023-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_DRPC_TYPES_H__
#define __DAOS_DRPC_TYPES_H__

#include <stddef.h>
#include <stdint.h>

/*
 * Using a packetsocket over the unix domain socket means that we receive
 * a whole message at a time without knowing its size. So for this reason
 * we need to restrict the maximum message size so we can preallocate a
 * buffer to put all of the information in.
 */
#define UNIXCOMM_MAXMSGSIZE (1 << 17)

/**
 * drpc_header precedes every dRPC message chunk.
 */
struct drpc_header {
	size_t   total_data_size;
	size_t   chunk_data_size;
	uint32_t chunk_idx;
	uint32_t total_chunks;
};

#define DRPC_HEADER_LEN sizeof(struct drpc_header)
#define DRPC_CHUNK_SIZE(bytes_left)                                                                \
	(bytes_left > DRPC_MAX_DATA_SIZE ? UNIXCOMM_MAXMSGSIZE : bytes_left + DRPC_HEADER_LEN)
#define DRPC_CHUNK_DATA_SIZE(bytes) (bytes - DRPC_HEADER_LEN)
#define DRPC_MAX_DATA_SIZE          DRPC_CHUNK_DATA_SIZE(UNIXCOMM_MAXMSGSIZE)
#define DRPC_CHUNK_DATA_PTR(ptr)    (ptr + DRPC_HEADER_LEN)

#endif /* __DAOS_DRPC_TYPES_H__ */
