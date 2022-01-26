/**
 * (C) Copyright 2021-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_PAR_LIB_H__
#define __DAOS_PAR_LIB_H__
#include <stdbool.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DPAR_MAJOR 1
#define DPAR_MINOR 0

#define DPAR_VERSION_SHIFT	16
#define DPAR_VERSION_MASK	(((uint32_t)1 << 16) - 1)
#define DPAR_VERSION (((uint32_t)DPAR_MAJOR << 16) | DPAR_MINOR)

static inline bool
par_version_compatible(uint32_t version)
{
	uint32_t major = version >> DPAR_VERSION_SHIFT;
	uint32_t minor = version & DPAR_VERSION_MASK;

	if (major != DPAR_MAJOR) /* Total incompatibility */
		return false;

	if (minor < DPAR_MINOR) /* An API we use doesn't exist in other library */
		return false;

	return true;
}

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

/** Retrieve the version */
uint32_t
par_getversion(void);

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

