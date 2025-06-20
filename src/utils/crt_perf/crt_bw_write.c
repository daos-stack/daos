/*
 * (C) Copyright 2023-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "crt_perf.h"

/****************/
/* Local Macros */
/****************/
#define BENCHMARK_NAME "Write BW (server bulk pull)"

/************************************/
/* Local Type and Struct Definition */
/************************************/

/********************/
/* Local Prototypes */
/********************/

static int
crt_perf_run(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info,
	     size_t buf_size, size_t skip);

/*******************/
/* Local Variables */
/*******************/

static int
crt_perf_run(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info,
	     size_t buf_size, size_t skip)
{
	struct timespec             t1, t2;
	size_t                      i;
	int                         rc;
	const struct crt_perf_opts *opts = &perf_info->opts;

	/* Warm up for RPC */
	for (i = 0; i < skip + (size_t)opts->loop; i++) {
		struct crt_perf_request args = {.expected_count = (int32_t)opts->request_max,
						.complete_count = 0,
						.rc             = 0,
						.done           = false,
						.cb             = NULL,
						.arg            = NULL};
		unsigned int            j;

		if (i == skip) {
			if (perf_info->mpi_info.size > 1)
				crt_perf_mpi_barrier(&perf_info->mpi_info);
			d_gettime(&t1);
		}

		for (j = 0; j < opts->request_max; j++) {
			struct crt_perf_rpc       *request = &info->requests[j];
			struct crt_perf_bulk_info *in_struct;

			rc = crt_req_create(info->context, &request->endpoint,
					    CRT_PERF_ID(CRT_PERF_BW_WRITE), &request->rpc);
			CRT_PERF_CHECK_D_ERROR(error, rc, "could not create request");

			in_struct = crt_req_get(request->rpc);
			in_struct->handle_id =
			    (uint32_t)((perf_info->mpi_info.rank + j * perf_info->mpi_info.size) /
				       (perf_info->ep_ranks * perf_info->ep_tags));
			in_struct->size = (uint32_t)buf_size;

			rc = crt_req_send(request->rpc, crt_perf_request_complete, &args);
			CRT_PERF_CHECK_D_ERROR(error, rc,
					       "could not send request to %" PRIu32 ":%" PRIu32,
					       request->endpoint.ep_rank, request->endpoint.ep_tag);
		}

		rc = crt_perf_request_wait(perf_info, info, CRT_PERF_TIMEOUT, &args);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not wait for requests");
	}

	if (perf_info->mpi_info.size > 1)
		crt_perf_mpi_barrier(&perf_info->mpi_info);

	d_gettime(&t2);

	if (perf_info->mpi_info.rank == 0)
		crt_perf_print_bw(perf_info, info, buf_size, d_timediff(&t1, &t2));

	return 0;

error:
	return rc;
}

int
main(int argc, char *argv[])
{
	struct crt_perf_info          perf_info;
	struct crt_perf_context_info *info;
	size_t                        size;
	int                           rc;

	/* Initialize the interface */
	rc = crt_perf_init(argc, argv, false, &perf_info);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not initialize");

	info = &perf_info.context_info[0];

	/* Set RPC requests */
	crt_perf_rpc_set_req(&perf_info, info);

	/* Allocate bulk buffers */
	rc = crt_perf_bulk_buf_init(&perf_info, info, CRT_BULK_GET);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not init bulk buffers");

	/* Header info */
	if (perf_info.mpi_info.rank == 0)
		crt_perf_print_header_bw(&perf_info, info, BENCHMARK_NAME);

	/* Bulk RPC with different sizes */
	for (size = MAX(1, perf_info.opts.buf_size_min); size <= perf_info.opts.buf_size_max;
	     size *= 2) {
		rc = crt_perf_run(&perf_info, info, size,
				  (size > CRT_PERF_LARGE_SIZE) ? CRT_PERF_LAT_SKIP_LARGE
							       : CRT_PERF_LAT_SKIP_SMALL);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not measure perf for size %zu", size);
	}

	/* Finalize interface */
	if (perf_info.mpi_info.rank == 0)
		crt_perf_send_done(&perf_info, info);

	crt_perf_cleanup(&perf_info);

	return EXIT_SUCCESS;

error:
	crt_perf_cleanup(&perf_info);

	return EXIT_FAILURE;
}
