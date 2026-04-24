/*
 * (C) Copyright 2023-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "crt_perf_mpi.h"

#include <dlfcn.h>

/****************/
/* Local Macros */
/****************/

#define MPI_SUCCESS 0

/************************************/
/* Local Type and Struct Definition */
/************************************/

enum crt_perf_mpi_impl { CRT_PERF_MPI_NONE, CRT_PERF_MPI_MPICH, CRT_PERF_MPI_OMPI };

union crt_perf_mpi_dtype {
	void *ompi;
	int   mpich;
};

#define CRT_PERF_MPICH_SYMS                                                                        \
	X(Init, (int *argc, char ***argv))                                                         \
	X(Finalize, (void))                                                                        \
	X(Initialized, (int *flag))                                                                \
	X(Finalized, (int *flag))                                                                  \
	X(Comm_size, (int comm, int *size))                                                        \
	X(Comm_rank, (int comm, int *rank))                                                        \
	X(Comm_dup, (int comm, int *newcomm))                                                      \
	X(Comm_free, (int *comm))                                                                  \
	X(Barrier, (int comm))                                                                     \
	X(Bcast, (void *buffer, int count, int datatype, int root, int comm))                      \
	X(Allgather, (const void *sendbuf, int sendcount, int sendtype, void *recvbuf,             \
		      int recvcount, int recvtype, int comm))

#define CRT_PERF_OMPI_SYMS                                                                         \
	X(Init, (int *argc, char ***argv))                                                         \
	X(Finalize, (void))                                                                        \
	X(Initialized, (int *flag))                                                                \
	X(Finalized, (int *flag))                                                                  \
	X(Comm_size, (void *comm, int *size))                                                      \
	X(Comm_rank, (void *comm, int *rank))                                                      \
	X(Comm_dup, (void *comm, void **newcomm))                                                  \
	X(Comm_free, (void **comm))                                                                \
	X(Barrier, (void *comm))                                                                   \
	X(Bcast, (void *buffer, int count, void *datatype, int root, void *comm))                  \
	X(Allgather, (const void *sendbuf, int sendcount, void *sendtype, void *recvbuf,           \
		      int recvcount, void *recvtype, void *comm))

#define X(a, b) int(*a) b;
struct crt_perf_mpich_funcs {
	CRT_PERF_MPICH_SYMS
};
struct crt_perf_ompi_funcs {
	CRT_PERF_OMPI_SYMS
};
#undef X

/********************/
/* Local Prototypes */
/********************/

static void
crt_perf_mpi_init_lib(void) __attribute__((__constructor__));

/*******************/
/* Local Variables */
/*******************/

#define X(a, b) .a = NULL,
static struct crt_perf_mpich_funcs crt_perf_mpich_funcs = {CRT_PERF_MPICH_SYMS};
static struct crt_perf_ompi_funcs  crt_perf_ompi_funcs  = {CRT_PERF_OMPI_SYMS};
#undef X

static const char *const crt_perf_mpi_lib_names[] = {"libmpi.so", "libmpi.so.12", "libmpi.so.40",
						     NULL};

static enum crt_perf_mpi_impl   crt_perf_mpi_impl = CRT_PERF_MPI_NONE;

/* MPICH constants */
static int                      crt_perf_mpich_comm_world = (int)0x44000000;
static int                      crt_perf_mpich_byte       = (int)0x4c00010d;

/* OMPI constants */
static void                    *crt_perf_ompi_comm_world = NULL;
static void                    *crt_perf_ompi_byte       = NULL;

static union crt_perf_mpi_comm  crt_perf_mpi_comm_world;
static union crt_perf_mpi_dtype crt_perf_mpi_byte;

static void
crt_perf_mpi_init_lib(void)
{
	void *dl_handle = NULL;
	int   i;

	for (i = 0; dl_handle == NULL && crt_perf_mpi_lib_names[i] != NULL; i++)
		dl_handle = dlopen(crt_perf_mpi_lib_names[i], RTLD_LAZY);
	if (dl_handle == NULL)
		goto error;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"

	/* Look for openmpi comm world */
	crt_perf_ompi_comm_world = dlsym(dl_handle, "ompi_mpi_comm_world");
	if (crt_perf_ompi_comm_world != NULL) {
		crt_perf_ompi_byte = dlsym(dl_handle, "ompi_mpi_byte");
		if (crt_perf_ompi_byte == NULL)
			goto error;

#define X(a, b)                                                                                    \
	crt_perf_ompi_funcs.a = dlsym(dl_handle, "MPI_" #a);                                       \
	if (crt_perf_ompi_funcs.a == NULL)                                                         \
		goto error;
		do {
			CRT_PERF_OMPI_SYMS
		} while (0);
#undef X

		crt_perf_mpi_impl            = CRT_PERF_MPI_OMPI;
		crt_perf_mpi_comm_world.ompi = crt_perf_ompi_comm_world;
		crt_perf_mpi_byte.ompi       = crt_perf_ompi_byte;
	} else {
#define X(a, b)                                                                                    \
	crt_perf_mpich_funcs.a = dlsym(dl_handle, "MPI_" #a);                                      \
	if (crt_perf_mpich_funcs.a == NULL)                                                        \
		goto error;
		do {
			CRT_PERF_MPICH_SYMS
		} while (0);
#undef X

		crt_perf_mpi_impl             = CRT_PERF_MPI_MPICH;
		crt_perf_mpi_comm_world.mpich = crt_perf_mpich_comm_world;
		crt_perf_mpi_byte.mpich       = crt_perf_mpich_byte;
	}

#pragma GCC diagnostic pop

	return;

error:
	return;
}

static int
crt_perf_mpi_Init(int *argc, char ***argv)
{
	switch (crt_perf_mpi_impl) {
	case CRT_PERF_MPI_MPICH:
		return crt_perf_mpich_funcs.Init(argc, argv);
	case CRT_PERF_MPI_OMPI:
		return crt_perf_ompi_funcs.Init(argc, argv);
	case CRT_PERF_MPI_NONE:
	default:
		return -1;
	}
}

static int
crt_perf_mpi_Finalize(void)
{
	switch (crt_perf_mpi_impl) {
	case CRT_PERF_MPI_MPICH:
		return crt_perf_mpich_funcs.Finalize();
	case CRT_PERF_MPI_OMPI:
		return crt_perf_ompi_funcs.Finalize();
	case CRT_PERF_MPI_NONE:
	default:
		return -1;
	}
}

static int
crt_perf_mpi_Initialized(int *flag)
{
	switch (crt_perf_mpi_impl) {
	case CRT_PERF_MPI_MPICH:
		return crt_perf_mpich_funcs.Initialized(flag);
	case CRT_PERF_MPI_OMPI:
		return crt_perf_ompi_funcs.Initialized(flag);
	case CRT_PERF_MPI_NONE:
	default:
		return -1;
	}
}

static int
crt_perf_mpi_Finalized(int *flag)
{
	switch (crt_perf_mpi_impl) {
	case CRT_PERF_MPI_MPICH:
		return crt_perf_mpich_funcs.Finalized(flag);
	case CRT_PERF_MPI_OMPI:
		return crt_perf_ompi_funcs.Finalized(flag);
	case CRT_PERF_MPI_NONE:
	default:
		return -1;
	}
}

static int
crt_perf_mpi_Comm_size(const union crt_perf_mpi_comm *comm, int *size)
{
	switch (crt_perf_mpi_impl) {
	case CRT_PERF_MPI_MPICH:
		return crt_perf_mpich_funcs.Comm_size(comm->mpich, size);
	case CRT_PERF_MPI_OMPI:
		return crt_perf_ompi_funcs.Comm_size(comm->ompi, size);
	case CRT_PERF_MPI_NONE:
	default:
		return -1;
	}
}

static int
crt_perf_mpi_Comm_rank(const union crt_perf_mpi_comm *comm, int *rank)
{
	switch (crt_perf_mpi_impl) {
	case CRT_PERF_MPI_MPICH:
		return crt_perf_mpich_funcs.Comm_rank(comm->mpich, rank);
	case CRT_PERF_MPI_OMPI:
		return crt_perf_ompi_funcs.Comm_rank(comm->ompi, rank);
	case CRT_PERF_MPI_NONE:
	default:
		return -1;
	}
}

static int
crt_perf_mpi_Comm_dup(const union crt_perf_mpi_comm *comm, union crt_perf_mpi_comm *newcomm)
{
	switch (crt_perf_mpi_impl) {
	case CRT_PERF_MPI_MPICH:
		return crt_perf_mpich_funcs.Comm_dup(comm->mpich, &newcomm->mpich);
	case CRT_PERF_MPI_OMPI:
		return crt_perf_ompi_funcs.Comm_dup(comm->ompi, &newcomm->ompi);
	case CRT_PERF_MPI_NONE:
	default:
		return -1;
	}
}

static int
crt_perf_mpi_Comm_free(union crt_perf_mpi_comm *comm)
{
	switch (crt_perf_mpi_impl) {
	case CRT_PERF_MPI_MPICH:
		return crt_perf_mpich_funcs.Comm_free(&comm->mpich);
	case CRT_PERF_MPI_OMPI:
		return crt_perf_ompi_funcs.Comm_free(&comm->ompi);
	case CRT_PERF_MPI_NONE:
	default:
		return -1;
	}
}

static int
crt_perf_mpi_Barrier(const union crt_perf_mpi_comm *comm)
{
	switch (crt_perf_mpi_impl) {
	case CRT_PERF_MPI_MPICH:
		return crt_perf_mpich_funcs.Barrier(comm->mpich);
	case CRT_PERF_MPI_OMPI:
		return crt_perf_ompi_funcs.Barrier(comm->ompi);
	case CRT_PERF_MPI_NONE:
	default:
		return -1;
	}
}

static int
crt_perf_mpi_Bcast(void *buffer, int count, const union crt_perf_mpi_dtype *datatype, int root,
		   const union crt_perf_mpi_comm *comm)
{
	switch (crt_perf_mpi_impl) {
	case CRT_PERF_MPI_MPICH:
		return crt_perf_mpich_funcs.Bcast(buffer, count, datatype->mpich, root,
						  comm->mpich);
	case CRT_PERF_MPI_OMPI:
		return crt_perf_ompi_funcs.Bcast(buffer, count, datatype->ompi, root, comm->ompi);
	case CRT_PERF_MPI_NONE:
	default:
		return -1;
	}
}

static int
crt_perf_mpi_Allgather(const void *sendbuf, int sendcount, const union crt_perf_mpi_dtype *sendtype,
		       void *recvbuf, int recvcount, const union crt_perf_mpi_dtype *recvtype,
		       const union crt_perf_mpi_comm *comm)
{
	switch (crt_perf_mpi_impl) {
	case CRT_PERF_MPI_MPICH:
		return crt_perf_mpich_funcs.Allgather(sendbuf, sendcount, sendtype->mpich, recvbuf,
						      recvcount, recvtype->mpich, comm->mpich);
	case CRT_PERF_MPI_OMPI:
		return crt_perf_ompi_funcs.Allgather(sendbuf, sendcount, sendtype->ompi, recvbuf,
						     recvcount, recvtype->ompi, comm->ompi);
	case CRT_PERF_MPI_NONE:
	default:
		return -1;
	}
}

int
crt_perf_mpi_init(struct crt_perf_mpi_info *mpi_info)
{
	int mpi_initialized = 0;
	int rc;

	/* Silently exit if MPI is not detected */
	if (crt_perf_mpi_impl == CRT_PERF_MPI_NONE) {
		mpi_info->size            = 1;
		mpi_info->rank            = 0;
		mpi_info->mpi_no_finalize = true;

		return 0;
	}

	rc = crt_perf_mpi_Initialized(&mpi_initialized);
	CRT_PERF_CHECK_ERROR(rc != MPI_SUCCESS, error, rc, -DER_MISC, "MPI_Initialized() failed");
	CRT_PERF_CHECK_ERROR(mpi_initialized, error, rc, -DER_MISC, "MPI was already initialized");

	rc = crt_perf_mpi_Init(NULL, NULL);
	CRT_PERF_CHECK_ERROR(rc != MPI_SUCCESS, error, rc, -DER_MISC, "MPI_Init() failed");

	rc = crt_perf_mpi_Comm_dup(&crt_perf_mpi_comm_world, &mpi_info->comm);
	CRT_PERF_CHECK_ERROR(rc != MPI_SUCCESS, error, rc, -DER_MISC, "MPI_Comm_dup() failed");

	rc = crt_perf_mpi_Comm_rank(&mpi_info->comm, &mpi_info->rank);
	CRT_PERF_CHECK_ERROR(rc != MPI_SUCCESS, error, rc, -DER_MISC, "MPI_Comm_rank() failed");

	rc = crt_perf_mpi_Comm_size(&mpi_info->comm, &mpi_info->size);
	CRT_PERF_CHECK_ERROR(rc != MPI_SUCCESS, error, rc, -DER_MISC, "MPI_Comm_size() failed");

	return 0;

error:
	crt_perf_mpi_finalize(mpi_info);

	return rc;
}

void
crt_perf_mpi_finalize(struct crt_perf_mpi_info *mpi_info)
{
	int mpi_finalized = 0;

	if (mpi_info->mpi_no_finalize)
		return;

	(void)crt_perf_mpi_Finalized(&mpi_finalized);
	if (mpi_finalized)
		return;

	(void)crt_perf_mpi_Comm_free(&mpi_info->comm);

	(void)crt_perf_mpi_Finalize();
}

int
crt_perf_mpi_barrier(const struct crt_perf_mpi_info *mpi_info)
{
	int rc;

	rc = crt_perf_mpi_Barrier(&mpi_info->comm);
	CRT_PERF_CHECK_ERROR(rc != MPI_SUCCESS, error, rc, -DER_MISC, "MPI_Barrier() failed");

	return 0;

error:
	return rc;
}

int
crt_perf_mpi_bcast(const struct crt_perf_mpi_info *mpi_info, void *buffer, size_t size, int root)
{
	int rc;

	rc = crt_perf_mpi_Bcast(buffer, (int)size, &crt_perf_mpi_byte, root, &mpi_info->comm);
	CRT_PERF_CHECK_ERROR(rc != MPI_SUCCESS, error, rc, -DER_MISC, "MPI_Bcast() failed");

	return 0;

error:
	return rc;
}

int
crt_perf_mpi_allgather(const struct crt_perf_mpi_info *mpi_info, const void *sendbuf,
		       size_t sendsize, void *recvbuf, size_t recvsize)
{
	int rc;

	rc = crt_perf_mpi_Allgather(sendbuf, (int)sendsize, &crt_perf_mpi_byte, recvbuf, recvsize,
				    &crt_perf_mpi_byte, &mpi_info->comm);
	CRT_PERF_CHECK_ERROR(rc != MPI_SUCCESS, error, rc, -DER_MISC, "MPI_Allgather() failed");

	return 0;

error:
	return rc;
}
