/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "na_test.h"

#ifdef HG_TEST_HAS_PARALLEL
#    ifdef NA_HAS_MPI
#        include "na_mpi.h"
#    endif
#else
#    include "mercury_dl.h"
#    define MPI_SUCCESS         0
#    define MPI_THREAD_MULTIPLE 3
#endif

#include "mercury_util.h"

/****************/
/* Local Macros */
/****************/

/************************************/
/* Local Type and Struct Definition */
/************************************/

enum na_test_mpi_impl {
    NA_TEST_MPI_NONE,
#ifdef HG_TEST_HAS_PARALLEL
    NA_TEST_MPI_SYSTEM,
#else
    NA_TEST_MPI_MPICH,
    NA_TEST_MPI_OMPI
#endif
};

union na_test_mpi_dtype {
#ifdef HG_TEST_HAS_PARALLEL
    MPI_Datatype sys;
#else
    void *ompi;
    int mpich;
#endif
};

#ifdef HG_TEST_HAS_PARALLEL
#    define NA_TEST_MPI_SYMS                                                   \
        X(Init, (int *argc, char ***argv))                                     \
        X(Init_thread, (int *argc, char ***argv, int required, int *provided)) \
        X(Finalize, (void) )                                                   \
        X(Initialized, (int *flag))                                            \
        X(Finalized, (int *flag))                                              \
        X(Comm_size, (MPI_Comm comm, int *size))                               \
        X(Comm_rank, (MPI_Comm comm, int *rank))                               \
        X(Comm_split, (MPI_Comm comm, int color, int key, MPI_Comm *newcomm))  \
        X(Comm_dup, (MPI_Comm comm, MPI_Comm * newcomm))                       \
        X(Comm_free, (MPI_Comm * comm))                                        \
        X(Barrier, (MPI_Comm comm))                                            \
        X(Bcast, (void *buffer, int count, MPI_Datatype datatype, int root,    \
                     MPI_Comm comm))

#    define X(a, b) int(*a) b;
struct na_test_mpi_funcs {
    NA_TEST_MPI_SYMS
};
#    undef X

#    define X(a, b) .a = MPI_##a,
static struct na_test_mpi_funcs na_test_mpi_funcs = {NA_TEST_MPI_SYMS};
#    undef X
#else
#    define NA_TEST_MPICH_SYMS                                                 \
        X(Init, (int *argc, char ***argv))                                     \
        X(Init_thread, (int *argc, char ***argv, int required, int *provided)) \
        X(Finalize, (void) )                                                   \
        X(Initialized, (int *flag))                                            \
        X(Finalized, (int *flag))                                              \
        X(Comm_size, (int comm, int *size))                                    \
        X(Comm_rank, (int comm, int *rank))                                    \
        X(Comm_split, (int comm, int color, int key, int *newcomm))            \
        X(Comm_dup, (int comm, int *newcomm))                                  \
        X(Comm_free, (int *comm))                                              \
        X(Barrier, (int comm))                                                 \
        X(Bcast, (void *buffer, int count, int datatype, int root, int comm))

#    define NA_TEST_OMPI_SYMS                                                  \
        X(Init, (int *argc, char ***argv))                                     \
        X(Init_thread, (int *argc, char ***argv, int required, int *provided)) \
        X(Finalize, (void) )                                                   \
        X(Initialized, (int *flag))                                            \
        X(Finalized, (int *flag))                                              \
        X(Comm_size, (void *comm, int *size))                                  \
        X(Comm_rank, (void *comm, int *rank))                                  \
        X(Comm_split, (void *comm, int color, int key, void **newcomm))        \
        X(Comm_dup, (void *comm, void **newcomm))                              \
        X(Comm_free, (void **comm))                                            \
        X(Barrier, (void *comm))                                               \
        X(Bcast,                                                               \
            (void *buffer, int count, void *datatype, int root, void *comm))

#    define X(a, b) int(*a) b;
struct na_test_mpich_funcs {
    NA_TEST_MPICH_SYMS
};
struct na_test_ompi_funcs {
    NA_TEST_OMPI_SYMS
};
#    undef X

#    define X(a, b) .a = NULL,
static struct na_test_mpich_funcs na_test_mpich_funcs = {NA_TEST_MPICH_SYMS};
static struct na_test_ompi_funcs na_test_ompi_funcs = {NA_TEST_OMPI_SYMS};
#    undef X

static const char *const na_test_mpi_lib_names[] = {
    "libmpi.so", "libmpi.so.12", "libmpi.so.40", NULL};
#endif

/********************/
/* Local Prototypes */
/********************/

#ifndef HG_TEST_HAS_PARALLEL
static void
na_test_mpi_init_lib(void) HG_ATTR_CONSTRUCTOR;
#endif

/*******************/
/* Local Variables */
/*******************/

#ifdef HG_TEST_HAS_PARALLEL
static enum na_test_mpi_impl na_test_mpi_impl = NA_TEST_MPI_SYSTEM;

static union na_test_mpi_comm na_test_mpi_comm_world = {.sys = MPI_COMM_WORLD};
static union na_test_mpi_dtype na_test_mpi_byte = {.sys = MPI_BYTE};
#else
static enum na_test_mpi_impl na_test_mpi_impl = NA_TEST_MPI_NONE;

/* MPICH constants */
static int na_test_mpich_comm_world = (int) 0x44000000;
static int na_test_mpich_byte = (int) 0x4c00010d;

/* OMPI constants */
static void *na_test_ompi_comm_world = NULL;
static void *na_test_ompi_byte = NULL;

static union na_test_mpi_comm na_test_mpi_comm_world;
static union na_test_mpi_dtype na_test_mpi_byte;

static void
na_test_mpi_init_lib(void)
{
    HG_DL_HANDLE dl_handle = NULL;
    int i;

    for (i = 0; dl_handle == NULL && na_test_mpi_lib_names[i] != NULL; i++)
        dl_handle = hg_dl_open(na_test_mpi_lib_names[i]);
    NA_TEST_CHECK_ERROR_NORET(
        dl_handle == NULL, error, "Could not find libmpi.so");

#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wpedantic"

    /* Look for openmpi comm world */
    na_test_ompi_comm_world = hg_dl_sym(dl_handle, "ompi_mpi_comm_world");
    if (na_test_ompi_comm_world != NULL) {
        na_test_ompi_byte = hg_dl_sym(dl_handle, "ompi_mpi_byte");
        NA_TEST_CHECK_ERROR_NORET(
            na_test_ompi_byte == NULL, error, "Could not find MPI_BYTE");

#    define X(a, b)                                                            \
        na_test_ompi_funcs.a = hg_dl_sym(dl_handle, "MPI_" #a);                \
        NA_TEST_CHECK_ERROR_NORET(                                             \
            na_test_ompi_funcs.a == NULL, error, "Could not find MPI_" #a);
        do {
            NA_TEST_OMPI_SYMS
        } while (0);
#    undef X

        na_test_mpi_impl = NA_TEST_MPI_OMPI;
        na_test_mpi_comm_world.ompi = na_test_ompi_comm_world;
        na_test_mpi_byte.ompi = na_test_ompi_byte;
    } else {
#    define X(a, b)                                                            \
        na_test_mpich_funcs.a = hg_dl_sym(dl_handle, "MPI_" #a);               \
        NA_TEST_CHECK_ERROR_NORET(                                             \
            na_test_mpich_funcs.a == NULL, error, "Could not find MPI_" #a);
        do {
            NA_TEST_MPICH_SYMS
        } while (0);
#    undef X

        na_test_mpi_impl = NA_TEST_MPI_MPICH;
        na_test_mpi_comm_world.mpich = na_test_mpich_comm_world;
        na_test_mpi_byte.mpich = na_test_mpich_byte;
    }

#    pragma GCC diagnostic pop

    return;

error:
    return;
}
#endif

/*---------------------------------------------------------------------------*/
static int
na_test_mpi_Init(int *argc, char ***argv)
{
    switch (na_test_mpi_impl) {
#ifdef HG_TEST_HAS_PARALLEL
        case NA_TEST_MPI_SYSTEM:
            return na_test_mpi_funcs.Init(argc, argv);
#else
        case NA_TEST_MPI_MPICH:
            return na_test_mpich_funcs.Init(argc, argv);
        case NA_TEST_MPI_OMPI:
            return na_test_ompi_funcs.Init(argc, argv);
#endif
        case NA_TEST_MPI_NONE:
        default:
            return -1;
    }
}

/*---------------------------------------------------------------------------*/
static int
na_test_mpi_Init_thread(int *argc, char ***argv, int required, int *provided)
{
    switch (na_test_mpi_impl) {
#ifdef HG_TEST_HAS_PARALLEL
        case NA_TEST_MPI_SYSTEM:
            return na_test_mpi_funcs.Init_thread(
                argc, argv, required, provided);
#else
        case NA_TEST_MPI_MPICH:
            return na_test_mpich_funcs.Init_thread(
                argc, argv, required, provided);
        case NA_TEST_MPI_OMPI:
            return na_test_ompi_funcs.Init_thread(
                argc, argv, required, provided);
#endif
        case NA_TEST_MPI_NONE:
        default:
            return -1;
    }
}

/*---------------------------------------------------------------------------*/
static int
na_test_mpi_Finalize(void)
{
    switch (na_test_mpi_impl) {
#ifdef HG_TEST_HAS_PARALLEL
        case NA_TEST_MPI_SYSTEM:
            return na_test_mpi_funcs.Finalize();
#else
        case NA_TEST_MPI_MPICH:
            return na_test_mpich_funcs.Finalize();
        case NA_TEST_MPI_OMPI:
            return na_test_ompi_funcs.Finalize();
#endif
        case NA_TEST_MPI_NONE:
        default:
            return -1;
    }
}

/*---------------------------------------------------------------------------*/
static int
na_test_mpi_Initialized(int *flag)
{
    switch (na_test_mpi_impl) {
#ifdef HG_TEST_HAS_PARALLEL
        case NA_TEST_MPI_SYSTEM:
            return na_test_mpi_funcs.Initialized(flag);
#else
        case NA_TEST_MPI_MPICH:
            return na_test_mpich_funcs.Initialized(flag);
        case NA_TEST_MPI_OMPI:
            return na_test_ompi_funcs.Initialized(flag);
#endif
        case NA_TEST_MPI_NONE:
        default:
            return -1;
    }
}

/*---------------------------------------------------------------------------*/
static int
na_test_mpi_Finalized(int *flag)
{
    switch (na_test_mpi_impl) {
#ifdef HG_TEST_HAS_PARALLEL
        case NA_TEST_MPI_SYSTEM:
            return na_test_mpi_funcs.Finalized(flag);
#else
        case NA_TEST_MPI_MPICH:
            return na_test_mpich_funcs.Finalized(flag);
        case NA_TEST_MPI_OMPI:
            return na_test_ompi_funcs.Finalized(flag);
#endif
        case NA_TEST_MPI_NONE:
        default:
            return -1;
    }
}

/*---------------------------------------------------------------------------*/
static int
na_test_mpi_Comm_size(const union na_test_mpi_comm *comm, int *size)
{
    switch (na_test_mpi_impl) {
#ifdef HG_TEST_HAS_PARALLEL
        case NA_TEST_MPI_SYSTEM:
            return na_test_mpi_funcs.Comm_size(comm->sys, size);
#else
        case NA_TEST_MPI_MPICH:
            return na_test_mpich_funcs.Comm_size(comm->mpich, size);
        case NA_TEST_MPI_OMPI:
            return na_test_ompi_funcs.Comm_size(comm->ompi, size);
#endif
        case NA_TEST_MPI_NONE:
        default:
            return -1;
    }
}

/*---------------------------------------------------------------------------*/
static int
na_test_mpi_Comm_rank(const union na_test_mpi_comm *comm, int *rank)
{
    switch (na_test_mpi_impl) {
#ifdef HG_TEST_HAS_PARALLEL
        case NA_TEST_MPI_SYSTEM:
            return na_test_mpi_funcs.Comm_rank(comm->sys, rank);
#else
        case NA_TEST_MPI_MPICH:
            return na_test_mpich_funcs.Comm_rank(comm->mpich, rank);
        case NA_TEST_MPI_OMPI:
            return na_test_ompi_funcs.Comm_rank(comm->ompi, rank);
#endif
        case NA_TEST_MPI_NONE:
        default:
            return -1;
    }
}

/*---------------------------------------------------------------------------*/
static int
na_test_mpi_Comm_split(const union na_test_mpi_comm *comm, int color, int key,
    union na_test_mpi_comm *newcomm)
{
    switch (na_test_mpi_impl) {
#ifdef HG_TEST_HAS_PARALLEL
        case NA_TEST_MPI_SYSTEM:
            return na_test_mpi_funcs.Comm_split(
                comm->sys, color, key, &newcomm->sys);
#else
        case NA_TEST_MPI_MPICH:
            return na_test_mpich_funcs.Comm_split(
                comm->mpich, color, key, &newcomm->mpich);
        case NA_TEST_MPI_OMPI:
            return na_test_ompi_funcs.Comm_split(
                comm->ompi, color, key, &newcomm->ompi);
#endif
        case NA_TEST_MPI_NONE:
        default:
            return -1;
    }
}

/*---------------------------------------------------------------------------*/
static int
na_test_mpi_Comm_dup(
    const union na_test_mpi_comm *comm, union na_test_mpi_comm *newcomm)
{
    switch (na_test_mpi_impl) {
#ifdef HG_TEST_HAS_PARALLEL
        case NA_TEST_MPI_SYSTEM:
            return na_test_mpi_funcs.Comm_dup(comm->sys, &newcomm->sys);
#else
        case NA_TEST_MPI_MPICH:
            return na_test_mpich_funcs.Comm_dup(comm->mpich, &newcomm->mpich);
        case NA_TEST_MPI_OMPI:
            return na_test_ompi_funcs.Comm_dup(comm->ompi, &newcomm->ompi);
#endif
        case NA_TEST_MPI_NONE:
        default:
            return -1;
    }
}

/*---------------------------------------------------------------------------*/
static int
na_test_mpi_Comm_free(union na_test_mpi_comm *comm)
{
    switch (na_test_mpi_impl) {
#ifdef HG_TEST_HAS_PARALLEL
        case NA_TEST_MPI_SYSTEM:
            return na_test_mpi_funcs.Comm_free(&comm->sys);
#else
        case NA_TEST_MPI_MPICH:
            return na_test_mpich_funcs.Comm_free(&comm->mpich);
        case NA_TEST_MPI_OMPI:
            return na_test_ompi_funcs.Comm_free(&comm->ompi);
#endif
        case NA_TEST_MPI_NONE:
        default:
            return -1;
    }
}

/*---------------------------------------------------------------------------*/
static int
na_test_mpi_Barrier(const union na_test_mpi_comm *comm)
{
    switch (na_test_mpi_impl) {
#ifdef HG_TEST_HAS_PARALLEL
        case NA_TEST_MPI_SYSTEM:
            return na_test_mpi_funcs.Barrier(comm->sys);
#else
        case NA_TEST_MPI_MPICH:
            return na_test_mpich_funcs.Barrier(comm->mpich);
        case NA_TEST_MPI_OMPI:
            return na_test_ompi_funcs.Barrier(comm->ompi);
#endif
        case NA_TEST_MPI_NONE:
        default:
            return -1;
    }
}

/*---------------------------------------------------------------------------*/
static int
na_test_mpi_Bcast(void *buffer, int count,
    const union na_test_mpi_dtype *datatype, int root,
    const union na_test_mpi_comm *comm)
{
    switch (na_test_mpi_impl) {
#ifdef HG_TEST_HAS_PARALLEL
        case NA_TEST_MPI_SYSTEM:
            return na_test_mpi_funcs.Bcast(
                buffer, count, datatype->sys, root, comm->sys);
#else
        case NA_TEST_MPI_MPICH:
            return na_test_mpich_funcs.Bcast(
                buffer, count, datatype->mpich, root, comm->mpich);
        case NA_TEST_MPI_OMPI:
            return na_test_ompi_funcs.Bcast(
                buffer, count, datatype->ompi, root, comm->ompi);
#endif
        case NA_TEST_MPI_NONE:
        default:
            return -1;
    }
}

/*---------------------------------------------------------------------------*/
na_return_t
na_test_mpi_init(struct na_test_mpi_info *mpi_info, bool listen,
    bool use_threads, bool mpi_static)
{
    int mpi_initialized = 0;
    na_return_t ret;
    int rc;

    /* Silently exit if MPI is not detected */
    if (na_test_mpi_impl == NA_TEST_MPI_NONE) {
        mpi_info->size = 1;
        mpi_info->rank = 0;
        mpi_info->mpi_no_finalize = true;

        return NA_SUCCESS;
    }

    rc = na_test_mpi_Initialized(&mpi_initialized);
    NA_TEST_CHECK_ERROR(rc != MPI_SUCCESS, error, ret, NA_PROTOCOL_ERROR,
        "MPI_Initialized() failed");
    NA_TEST_CHECK_ERROR(mpi_initialized, error, ret, NA_PROTOCOL_ERROR,
        "MPI was already initialized");

#ifdef NA_MPI_HAS_GNI_SETUP
    /* Setup GNI job before initializing MPI */
    ret = NA_MPI_Gni_job_setup();
    NA_TEST_CHECK_NA_ERROR(error, ret, "Could not setup GNI job");
#endif

    if ((listen && use_threads) || mpi_static) {
        int provided;

        rc =
            na_test_mpi_Init_thread(NULL, NULL, MPI_THREAD_MULTIPLE, &provided);
        NA_TEST_CHECK_ERROR(rc != MPI_SUCCESS, error, ret, NA_PROTOCOL_ERROR,
            "MPI_Init_thread() failed");

        NA_TEST_CHECK_ERROR(provided != MPI_THREAD_MULTIPLE, error, ret,
            NA_PROTOCOL_ERROR, "MPI_THREAD_MULTIPLE cannot be set");

        /* Only if we do static MPMD MPI */
        if (mpi_static) {
            int color, global_rank;

            rc = na_test_mpi_Comm_rank(&na_test_mpi_comm_world, &global_rank);
            NA_TEST_CHECK_ERROR(rc != MPI_SUCCESS, error, ret,
                NA_PROTOCOL_ERROR, "MPI_Comm_rank() failed");

            /* Color is 1 for server, 2 for client */
            color = listen ? 1 : 2;

            /* Assume that the application did not split MPI_COMM_WORLD
             * already
             */
            rc = na_test_mpi_Comm_split(
                &na_test_mpi_comm_world, color, global_rank, &mpi_info->comm);
            NA_TEST_CHECK_ERROR(rc != MPI_SUCCESS, error, ret,
                NA_PROTOCOL_ERROR, "MPI_Comm_split() failed");

#ifdef NA_HAS_MPI /* implies HG_TEST_HAS_PARALLEL */
            /* Set init comm that will be used to setup NA MPI */
            NA_MPI_Set_init_intra_comm(mpi_info->comm.sys);
#endif
        } else {
            rc = na_test_mpi_Comm_dup(&na_test_mpi_comm_world, &mpi_info->comm);
            NA_TEST_CHECK_ERROR(rc != MPI_SUCCESS, error, ret,
                NA_PROTOCOL_ERROR, "MPI_Comm_dup() failed");
        }
    } else {
        rc = na_test_mpi_Init(NULL, NULL);
        NA_TEST_CHECK_ERROR(rc != MPI_SUCCESS, error, ret, NA_PROTOCOL_ERROR,
            "MPI_Init() failed");

        rc = na_test_mpi_Comm_dup(&na_test_mpi_comm_world, &mpi_info->comm);
        NA_TEST_CHECK_ERROR(rc != MPI_SUCCESS, error, ret, NA_PROTOCOL_ERROR,
            "MPI_Comm_dup() failed");
    }

    rc = na_test_mpi_Comm_rank(&mpi_info->comm, &mpi_info->rank);
    NA_TEST_CHECK_ERROR(rc != MPI_SUCCESS, error, ret, NA_PROTOCOL_ERROR,
        "MPI_Comm_rank() failed");

    rc = na_test_mpi_Comm_size(&mpi_info->comm, &mpi_info->size);
    NA_TEST_CHECK_ERROR(rc != MPI_SUCCESS, error, ret, NA_PROTOCOL_ERROR,
        "MPI_Comm_size() failed");

    return NA_SUCCESS;

error:
    na_test_mpi_finalize(mpi_info);

    return ret;
}

/*---------------------------------------------------------------------------*/
void
na_test_mpi_finalize(struct na_test_mpi_info *mpi_info)
{
    int mpi_finalized = 0;

    if (mpi_info->mpi_no_finalize)
        return;

    (void) na_test_mpi_Finalized(&mpi_finalized);
    if (mpi_finalized)
        return;

    (void) na_test_mpi_Comm_free(&mpi_info->comm);

    (void) na_test_mpi_Finalize();
}

/*---------------------------------------------------------------------------*/
na_return_t
na_test_mpi_barrier(const struct na_test_mpi_info *mpi_info)
{
    na_return_t ret;
    int rc;

    rc = na_test_mpi_Barrier(&mpi_info->comm);
    NA_TEST_CHECK_ERROR(rc != MPI_SUCCESS, error, ret, NA_PROTOCOL_ERROR,
        "MPI_Barrier() failed");

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
na_test_mpi_barrier_world(void)
{
    na_return_t ret;
    int rc;

    rc = na_test_mpi_Barrier(&na_test_mpi_comm_world);
    NA_TEST_CHECK_ERROR(rc != MPI_SUCCESS, error, ret, NA_PROTOCOL_ERROR,
        "MPI_Barrier() failed");

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
na_test_mpi_bcast(const struct na_test_mpi_info *mpi_info, void *buffer,
    size_t size, int root)
{
    na_return_t ret;
    int rc;

    rc = na_test_mpi_Bcast(
        buffer, (int) size, &na_test_mpi_byte, root, &mpi_info->comm);
    NA_TEST_CHECK_ERROR(
        rc != MPI_SUCCESS, error, ret, NA_PROTOCOL_ERROR, "MPI_Bcast() failed");

    return NA_SUCCESS;

error:
    return ret;
}
