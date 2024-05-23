/**
 * (C) Copyright 2021-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_PAR_LIB_H__
#define __DAOS_PAR_LIB_H__
#include <stdbool.h>
#include <inttypes.h>

#define DPAR_MAJOR 2
#define DPAR_MINOR 0

#define DPAR_VERSION_SHIFT	16
#define DPAR_VERSION_MASK	(((uint32_t)1 << 16) - 1)
#define DPAR_VERSION (((uint32_t)DPAR_MAJOR << 16) | DPAR_MINOR)

#define PAR_COMM_WORLD	0

/** Return true if the opened library is compatible with the client.  Not
 *  defined in stub library.
 */
bool
par_version_compatible(uint32_t version, uint32_t *libmajor, uint32_t *libminor);

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
par_barrier(uint32_t comm);

/** Get the global rank */
int
par_rank(uint32_t comm, int *rank);

/** Get the global size */
int
par_size(uint32_t comm, int *size);

/** Reduce from all ranks */
int
par_reduce(uint32_t comm, const void *sendbuf, void *recvbuf, int count, enum par_type type,
	   enum par_op op, int root);

/** Gather from all ranks */
int
par_gather(uint32_t comm, const void *sendbuf, void *recvbuf, int count, enum par_type type,
	   int root);

/** All reduce from all ranks */
int
par_allreduce(uint32_t comm, const void *sendbuf, void *recvbuf, int count, enum par_type type,
	      enum par_op op);

/** All gather from all ranks */
int
par_allgather(uint32_t comm, const void *sendbuf, void *recvbuf, int count, enum par_type type);

/** Broadcast to all ranks */
int
par_bcast(uint32_t comm, void *buffer, int count, enum par_type datatype, int root);

/** Split a communicator to create a new one */
int
par_comm_split(uint32_t comm, int color, int key, uint32_t *new_comm);

/** Free a communicator */
int
par_comm_free(uint32_t comm);

#endif /** __DAOS_PAR_LIB_H__ */

