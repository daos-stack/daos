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

#define CRT_PERF_HAS_DAOS_TSE 1
#ifdef CRT_PERF_HAS_DAOS_TSE
#include <daos/tse.h>
#endif

#include <sys/uio.h>

/*************************************/
/* Public Type and Struct Definition */
/*************************************/

struct crt_perf_opts {
	char  *comm;          /* Comm plugin (ofi/ucx/etc.) */
	char  *domain;        /* Domain / device to use */
	char  *protocol;      /* Protocol provider (tcp/verbs/etc.) */
	char  *hostname;      /* Hostname / IP address / interface to use */
	char  *port;          /* Port to use */
	char  *auth_key;      /* Auth key to use */
	char  *attach_path;   /* CART attach info path */
	char  *group_id;      /* CART group ID to use */
	size_t msg_size_max;  /* Max message size */
	size_t buf_size_min;  /* Min buffer size */
	size_t buf_size_max;  /* Max buffer size */
	size_t context_max;   /* Max number of contexts */
	size_t request_max;   /* Max number of requests */
	size_t buf_count;     /* Number of buffers */
	int    loop;          /* Number of loops */
	bool   busy_wait;     /* Busy wait */
	bool   bidir;         /* Bidirectional */
	bool   force_reg;     /* Force registration */
	bool   verify;        /* Verify data */
	bool   mbps;          /* Show MBps */
	bool   daos_agent;    /* Use DAOS agent */
	bool   progress_cond; /* Use crt_progress_cond to wait for requests */
	bool   daos_tse;      /* Use DAOS TSE (client) */
};

struct crt_perf_info {
	struct crt_perf_opts          opts;         /* Init options */
	struct crt_perf_context_info *context_info; /* Context info */
	struct crt_perf_mpi_info      mpi_info;     /* MPI comm info */
	crt_group_t                  *ep_group;     /* CRT group */
	uint32_t                      ep_ranks;     /* Number of ranks */
	uint32_t                      ep_tags;      /* Number of tags/contexts per rank */
};

struct crt_perf_context_info {
	crt_context_t             context;                /* CRT context */
	struct crt_perf_rpc      *requests;               /* Array of RPC requests */
	struct crt_perf_tse_task *tse_tasks;              /* Array of TSE tasks */
	tse_sched_t               tse_sched;              /* TSE scheduler */
	void                     *rpc_buf;                /* RPC buffer */
	void                    **bulk_bufs;              /* Array of bulk buffers */
	crt_bulk_t               *local_bulk_handles;     /* Array of local bulk handles */
	crt_bulk_t               *remote_bulk_handles;    /* Array of remote bulk handles */
	uint32_t                 *remote_bulk_handle_ids; /* Array of remote bulk handle IDs */
	struct crt_perf_request  *bulk_requests;          /* Array of bulk requests */
	size_t                    handle_max;             /* Max number of bulk handles */
	size_t                    buf_count;              /* Number of buffers */
	size_t                    buf_size_max;           /* Max buffer size */
	int                       context_id;             /* Context ID */
	bool                      done;                   /* Context finalized */
	bool                      verify;                 /* Verify data */
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
