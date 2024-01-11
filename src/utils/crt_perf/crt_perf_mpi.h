/*
 * (C) Copyright 2023-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __CRT_PERF_MPI_H__
#define __CRT_PERF_MPI_H__

#include "crt_perf_error.h"

#include <stddef.h>
#include <stdbool.h>

/*************************************/
/* Public Type and Struct Definition */
/*************************************/

union crt_perf_mpi_comm {
	void *ompi;
	int   mpich;
};

struct crt_perf_mpi_info {
	union crt_perf_mpi_comm comm;            /* MPI comm */
	int                     rank;            /* MPI comm rank */
	int                     size;            /* MPI comm size */
	bool                    mpi_no_finalize; /* Prevent from finalizing MPI */
};

/*****************/
/* Public Macros */
/*****************/

/*********************/
/* Public Prototypes */
/*********************/

int
crt_perf_mpi_init(struct crt_perf_mpi_info *mpi_info);

void
crt_perf_mpi_finalize(struct crt_perf_mpi_info *mpi_info);

int
crt_perf_mpi_barrier(const struct crt_perf_mpi_info *mpi_info);

int
crt_perf_mpi_bcast(const struct crt_perf_mpi_info *mpi_info, void *buffer, size_t size, int root);

int
crt_perf_mpi_allgather(const struct crt_perf_mpi_info *mpi_info, const void *sendbuf,
		       size_t sendsize, void *recvbuf, size_t recvsize);

#endif /* __CRT_PERF_MPI_H__ */
