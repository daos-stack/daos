/*
 * (C) Copyright 2023-2024 Intel Corporation.
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __CRT_PERF_H__
#define __CRT_PERF_H__

#include "crt_perf_mpi.h"

#include <cart/api.h>

#include <sys/uio.h>

/*************************************/
/* Public Type and Struct Definition */
/*************************************/

struct crt_perf_opts {
	char  *comm;
	char  *domain;
	char  *protocol;
	char  *hostname;
	char  *port;
	char  *auth_key;
	char  *attach_path;
	size_t msg_size_max;
	size_t buf_size_min;
	size_t buf_size_max;
	size_t context_max;
	size_t request_max;
	size_t buf_count;
	int    loop;
	bool   busy_wait;
	bool   bidir;
	bool   force_reg;
	bool   verify;
	bool   mbps;
};

struct crt_perf_info {
	struct crt_perf_opts          opts;
	struct crt_perf_context_info *context_info;
	struct crt_perf_mpi_info      mpi_info;
	crt_group_t                  *ep_group;
	uint32_t                      ep_ranks;
	uint32_t                      ep_tags;
};

struct crt_perf_context_info {
	crt_context_t            context;
	struct crt_perf_rpc     *requests;
	void                    *rpc_buf;
	void                   **bulk_bufs;
	crt_bulk_t              *local_bulk_handles;
	crt_bulk_t              *remote_bulk_handles;
	uint32_t                *remote_bulk_handle_ids;
	struct crt_perf_request *bulk_requests;
	size_t                   handle_max;
	size_t                   buf_count;
	size_t                   buf_size_max;
	int                      context_id;
	bool                     done;
};

/*****************/
/* Public Macros */
/*****************/

#define CRT_PERF_SKIP_SMALL 100
#define CRT_PERF_SKIP_LARGE 10
#define CRT_PERF_LARGE_SIZE 8192

#define CRT_PERF_TIMEOUT    (1000 * 1000) /* us */

/*********************/
/* Public Prototypes */
/*********************/

int
crt_perf_init(int argc, char *argv[], bool listen, struct crt_perf_info *info);

void
crt_perf_cleanup(struct crt_perf_info *info);

void
crt_perf_rpc_set_req(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info);

int
crt_perf_rpc_buf_init(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info);

int
crt_perf_bulk_buf_init(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info,
		       crt_bulk_op_t bulk_op);

void
crt_perf_print_header_lat(const struct crt_perf_info         *perf_info,
			  const struct crt_perf_context_info *info, const char *benchmark);

int
crt_perf_run_lat(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info,
		 size_t buf_size, size_t skip);

void
crt_perf_print_header_bw(const struct crt_perf_info         *perf_info,
			 const struct crt_perf_context_info *info, const char *benchmark);

int
crt_perf_run_bw(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info,
		size_t buf_size, size_t skip, crt_bulk_op_t bulk_op);

int
crt_perf_send_done(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info);

#endif /* __CRT_PERF_H__ */
