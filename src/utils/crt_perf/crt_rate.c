/*
 * (C) Copyright 2023-2024 Intel Corporation.
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "crt_perf.h"

/****************/
/* Local Macros */
/****************/
#define BENCHMARK_NAME "RPC rate"

/********************/
/* Local Prototypes */
/********************/

int
main(int argc, char **argv)
{
	struct crt_perf_info          perf_info;
	struct crt_perf_context_info *info;
	size_t                        size;
	int                           rc;

	/* Initialize the interface */
	rc = crt_perf_init(argc, argv, false, &perf_info);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not initialize");

	info = &perf_info.context_info[0];

	/* Allocate RPC buffers */
	rc = crt_perf_rpc_buf_init(&perf_info, info);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not init RPC buffers");

	/* Set RPC requests */
	crt_perf_rpc_set_req(&perf_info, info);

	/* Header info */
	if (perf_info.mpi_info.rank == 0)
		crt_perf_print_header_lat(&perf_info, info, BENCHMARK_NAME);

	/* NULL RPC */
	if (perf_info.opts.buf_size_min == 0) {
		rc = crt_perf_run_lat(&perf_info, info, 0, CRT_PERF_SKIP_SMALL);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not measure perf for size 0");
	}

	/* RPC with different sizes */
	for (size = MAX(1, perf_info.opts.buf_size_min); size <= perf_info.opts.buf_size_max;
	     size *= 2) {
		rc = crt_perf_run_lat(&perf_info, info, size,
				      (size > CRT_PERF_LARGE_SIZE) ? CRT_PERF_SKIP_LARGE
								   : CRT_PERF_SKIP_SMALL);
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
