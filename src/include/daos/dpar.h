/**
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_PAR_LIB_H__
#define __DAOS_PAR_LIB_H__

#ifdef __cplusplus
extern "C" {
#endif

enum par_type {
	PAR_INT		= 0,
	PAR_CHAR	= 1,
	PAR_BYTE	= 2,
	PAR_UINT64	= 3,
	PAR_DOUBLE	= 4,
};

enum par_op {
	PAR_MAX		= 0,
	PAR_MIN		= 1,
	PAR_SUM		= 2,
};

/** Initialize the library */
int
par_init(int *argc, char ***argv);

/** Finalize the library */
int
par_fini(void);

/** Barrier on all ranks */
int
par_barrier(void);

/** Get the global rank */
int
par_rank(int *rank);

/** Get the global size */
int
par_size(int *size);

/** Reduce from all ranks */
int
par_reduce(const void *sendbuf, void *recvbuf, int count, enum par_type type, enum par_op op,
	   int root);

/** Gather from all ranks */
int
par_gather(const void *sendbuf, void *recvbuf, int count, enum par_type type,
	   int root);

/** All reduce from all ranks */
int
par_allreduce(const void *sendbuf, void *recvbuf, int count, enum par_type type, enum par_op op);

/** All gather from all ranks */
int
par_allgather(const void *sendbuf, void *recvbuf, int count, enum par_type type);

/** Broadcast to all ranks */
int
par_bcast(void *buffer, int count, enum par_type datatype, int root);

#ifdef __cplusplus
}
#endif

#endif /** __DAOS_PAR_LIB_H__ */

