/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef NA_TEST_MPI_H
#define NA_TEST_MPI_H

#include "mercury_test_config.h"
#include "na.h"

#ifdef HG_TEST_HAS_PARALLEL
#    include <mpi.h>
#endif

/*************************************/
/* Public Type and Struct Definition */
/*************************************/

union na_test_mpi_comm {
#ifdef HG_TEST_HAS_PARALLEL
    MPI_Comm sys;
#else
    void *ompi;
    int mpich;
#endif
};

struct na_test_mpi_info {
    union na_test_mpi_comm comm; /* MPI comm */
    int rank;                    /* MPI comm rank */
    int size;                    /* MPI comm size */
    bool mpi_no_finalize;        /* Prevent from finalizing MPI */
};

/*****************/
/* Public Macros */
/*****************/

/*********************/
/* Public Prototypes */
/*********************/

#ifdef __cplusplus
extern "C" {
#endif

na_return_t
na_test_mpi_init(struct na_test_mpi_info *mpi_info, bool listen,
    bool use_threads, bool mpi_static);

void
na_test_mpi_finalize(struct na_test_mpi_info *mpi_info);

na_return_t
na_test_mpi_barrier(const struct na_test_mpi_info *mpi_info);

na_return_t
na_test_mpi_barrier_world(void);

na_return_t
na_test_mpi_bcast(const struct na_test_mpi_info *mpi_info, void *buffer,
    size_t size, int root);

#ifdef __cplusplus
}
#endif

#endif /* NA_TEST_MPI_H */
