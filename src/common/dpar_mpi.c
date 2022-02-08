#include <stdio.h>
#include <mpi.h>
#include <daos/dpar.h>

uint32_t
par_getversion(void)
{
	return DPAR_VERSION;
}

int
par_init(int *argc, char ***argv)
{
	int rc = MPI_Init(argc, argv);

	if (rc == MPI_SUCCESS)
		return 0;

	fprintf(stderr, "MPI_Init failed with %d\n", rc);
	return -1;
}

int
par_fini(void)
{
	int rc = MPI_Finalize();

	if (rc == MPI_SUCCESS)
		return 0;

	fprintf(stderr, "MPI_Finalize failed with %d\n", rc);
	return -1;
}

int
par_barrier(void)
{
	int rc = MPI_Barrier(MPI_COMM_WORLD);

	if (rc == MPI_SUCCESS)
		return 0;

	fprintf(stderr, "MPI_Barrier failed with %d\n", rc);
	return -1;
}

int
par_rank(int *rank)
{
	int rc = MPI_Comm_rank(MPI_COMM_WORLD, rank);

	if (rc == MPI_SUCCESS)
		return 0;

	fprintf(stderr, "MPI_Comm_rank failed with %d\n", rc);
	return -1;
}

int
par_size(int *size)
{
	int rc = MPI_Comm_size(MPI_COMM_WORLD, size);

	if (rc == MPI_SUCCESS)
		return 0;

	fprintf(stderr, "MPI_Comm_size failed with %d\n", rc);
	return -1;
}

static inline
MPI_Datatype
type_par2mpi(enum par_type type)
{
	switch (type) {
	case PAR_INT:
		return MPI_INT;
	case PAR_CHAR:
		return MPI_CHAR;
	case PAR_BYTE:
		return MPI_BYTE;
	case PAR_DOUBLE:
		return MPI_DOUBLE;
	case PAR_UINT64:
		return MPI_UINT64_T;
	default:
		fprintf(stderr, "Unknown datatype specified %d\n", type);
		return MPI_DATATYPE_NULL;
	};
}

static inline
MPI_Op
op_par2mpi(enum par_op op)
{
	switch (op) {
	case PAR_MAX:
		return MPI_MAX;
	case PAR_MIN:
		return MPI_MIN;
	case PAR_SUM:
		return MPI_SUM;
	default:
		fprintf(stderr, "Unknown op specified %d\n", op);
		return MPI_OP_NULL;
	};
}

int
par_reduce(const void *sendbuf, void *recvbuf, int count, enum par_type type, enum par_op op,
	   int root)
{
	MPI_Datatype	mtype = type_par2mpi(type);
	MPI_Op		mop = op_par2mpi(op);
	int		rc;

	if (mtype == MPI_DATATYPE_NULL)
		return -1;
	if (mop == MPI_OP_NULL)
		return -1;

	rc = MPI_Reduce(sendbuf, recvbuf, count, mtype, mop, root, MPI_COMM_WORLD);

	if (rc == MPI_SUCCESS)
		return 0;

	fprintf(stderr, "MPI_Reduce failed with %d\n", rc);
	return -1;
}

/** Gather from all ranks */
int
par_gather(const void *sendbuf, void *recvbuf, int count, enum par_type type,
	   int root)
{
	MPI_Datatype	mtype = type_par2mpi(type);
	int		rc;

	if (mtype == MPI_DATATYPE_NULL)
		return -1;

	rc = MPI_Gather(sendbuf, count, mtype, recvbuf, count, mtype, root, MPI_COMM_WORLD);

	if (rc == MPI_SUCCESS)
		return 0;

	fprintf(stderr, "MPI_Gather failed with %d\n", rc);
	return -1;
}

/** All reduce from all ranks */
int
par_allreduce(const void *sendbuf, void *recvbuf, int count, enum par_type type, enum par_op op)
{
	MPI_Datatype	mtype = type_par2mpi(type);
	MPI_Op		mop = op_par2mpi(op);
	int		rc;

	if (mtype == MPI_DATATYPE_NULL)
		return -1;
	if (mop == MPI_OP_NULL)
		return -1;

	rc = MPI_Allreduce(sendbuf, recvbuf, count, mtype, mop, MPI_COMM_WORLD);


	if (rc == MPI_SUCCESS)
		return 0;

	fprintf(stderr, "MPI_Allreduce failed with %d\n", rc);
	return -1;
}

/** All gather from all ranks */
int
par_allgather(const void *sendbuf, void *recvbuf, int count, enum par_type type)
{
	MPI_Datatype	mtype = type_par2mpi(type);
	int		rc;

	if (mtype == MPI_DATATYPE_NULL)
		return -1;

	rc = MPI_Allgather(sendbuf, count, mtype, recvbuf, count, mtype, MPI_COMM_WORLD);

	if (rc == MPI_SUCCESS)
		return 0;

	fprintf(stderr, "MPI_Allgather failed with %d\n", rc);
	return -1;
}

/** Broadcast to all ranks */
int
par_bcast(void *buffer, int count, enum par_type type, int root)
{
	MPI_Datatype	mtype = type_par2mpi(type);
	int		rc;

	if (mtype == MPI_DATATYPE_NULL)
		return -1;

	rc = MPI_Bcast(buffer, count, mtype, root, MPI_COMM_WORLD);

	if (rc == MPI_SUCCESS)
		return 0;

	fprintf(stderr, "MPI_Bcast failed with %d\n", rc);
	return -1;
}
