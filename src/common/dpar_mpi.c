#include <stdio.h>
#include <mpi.h>
#include <daos/dpar.h>
#include <gurt/atomic.h>

static ATOMIC uint64_t	comm_free_bits = 0xfffffffffffffffe;
static MPI_Comm		comm_table[64];

static int
pcom2comm(uint32_t pcom, MPI_Comm *comm)
{
	uint64_t idx_bit;

	if (pcom >= 64) {
		fprintf(stderr, "Invalid dpar communicator %d\n", pcom);
		return -1;
	}

	idx_bit = 1ULL << pcom;
	if (idx_bit & comm_free_bits) {
		fprintf(stderr, "Invalid dpar communicator %d\n", pcom);
		return -1;
	}

	*comm = comm_table[pcom];
	return 0;
}

uint32_t
par_getversion(void)
{
	return DPAR_VERSION;
}

int
par_init(int *argc, char ***argv)
{
	int rc = MPI_Init(argc, argv);

	comm_table[0] = MPI_COMM_WORLD;

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
par_barrier(uint32_t pcom)
{
	MPI_Comm	comm = {0};
	int		rc = pcom2comm(pcom, &comm);

	if (rc != 0)
		return rc;

	 rc = MPI_Barrier(comm);

	if (rc == MPI_SUCCESS)
		return 0;

	fprintf(stderr, "MPI_Barrier failed with %d\n", rc);
	return -1;
}

int
par_rank(uint32_t pcom, int *rank)
{
	MPI_Comm	comm = {0};
	int		rc = pcom2comm(pcom, &comm);

	if (rc != 0)
		return rc;

	rc = MPI_Comm_rank(comm, rank);

	if (rc == MPI_SUCCESS)
		return 0;

	fprintf(stderr, "MPI_Comm_rank failed with %d\n", rc);
	return -1;
}

int
par_size(uint32_t pcom, int *size)
{
	MPI_Comm	comm = {0};
	int		rc = pcom2comm(pcom, &comm);

	if (rc != 0)
		return rc;

	rc = MPI_Comm_size(comm, size);

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
par_reduce(uint32_t pcom, const void *sendbuf, void *recvbuf, int count, enum par_type type,
	   enum par_op op, int root)
{
	MPI_Comm	comm = {0};
	int		rc = pcom2comm(pcom, &comm);
	MPI_Datatype	mtype = type_par2mpi(type);
	MPI_Op		mop = op_par2mpi(op);

	if (rc != 0)
		return rc;

	if (mtype == MPI_DATATYPE_NULL)
		return -1;
	if (mop == MPI_OP_NULL)
		return -1;

	rc = MPI_Reduce(sendbuf, recvbuf, count, mtype, mop, root, comm);

	if (rc == MPI_SUCCESS)
		return 0;

	fprintf(stderr, "MPI_Reduce failed with %d\n", rc);
	return -1;
}

/** Gather from all ranks */
int
par_gather(uint32_t pcom, const void *sendbuf, void *recvbuf, int count, enum par_type type,
	   int root)
{
	MPI_Comm	comm = {0};
	MPI_Datatype	mtype = type_par2mpi(type);
	int		rc = pcom2comm(pcom, &comm);

	if (rc != 0)
		return rc;

	if (mtype == MPI_DATATYPE_NULL)
		return -1;

	rc = MPI_Gather(sendbuf, count, mtype, recvbuf, count, mtype, root, comm);

	if (rc == MPI_SUCCESS)
		return 0;

	fprintf(stderr, "MPI_Gather failed with %d\n", rc);
	return -1;
}

/** All reduce from all ranks */
int
par_allreduce(uint32_t pcom, const void *sendbuf, void *recvbuf, int count, enum par_type type,
	      enum par_op op)
{
	MPI_Comm	comm = {0};
	MPI_Datatype	mtype = type_par2mpi(type);
	MPI_Op		mop = op_par2mpi(op);
	int		rc = pcom2comm(pcom, &comm);

	if (rc != 0)
		return rc;

	if (mtype == MPI_DATATYPE_NULL)
		return -1;
	if (mop == MPI_OP_NULL)
		return -1;

	rc = MPI_Allreduce(sendbuf, recvbuf, count, mtype, mop, comm);


	if (rc == MPI_SUCCESS)
		return 0;

	fprintf(stderr, "MPI_Allreduce failed with %d\n", rc);
	return -1;
}

/** All gather from all ranks */
int
par_allgather(uint32_t pcom, const void *sendbuf, void *recvbuf, int count, enum par_type type)
{
	MPI_Comm	comm = {0};
	MPI_Datatype	mtype = type_par2mpi(type);
	int		rc = pcom2comm(pcom, &comm);

	if (rc != 0)
		return rc;

	if (mtype == MPI_DATATYPE_NULL)
		return -1;

	rc = MPI_Allgather(sendbuf, count, mtype, recvbuf, count, mtype, comm);

	if (rc == MPI_SUCCESS)
		return 0;

	fprintf(stderr, "MPI_Allgather failed with %d\n", rc);
	return -1;
}

/** Broadcast to all ranks */
int
par_bcast(uint32_t pcom, void *buffer, int count, enum par_type type, int root)
{
	MPI_Datatype	mtype = type_par2mpi(type);
	MPI_Comm	comm = {0};
	int		rc = pcom2comm(pcom, &comm);

	if (rc != 0)
		return rc;

	if (mtype == MPI_DATATYPE_NULL)
		return -1;

	rc = MPI_Bcast(buffer, count, mtype, root, comm);

	if (rc == MPI_SUCCESS)
		return 0;

	fprintf(stderr, "MPI_Bcast failed with %d\n", rc);
	return -1;
}

static int
alloc_pcom(uint32_t *pcom)
{
	int		idx = 0;
	uint64_t	old_value;
	uint64_t	new_value;

	do {
		if (comm_free_bits == 0) {
			fprintf(stderr, "No more available communicators\n");
			return -1;
		}

		old_value = comm_free_bits;

		idx = __builtin_ffs(old_value); /* idx of least significant 1 bit */
		new_value = old_value ^ (1ULL << idx);
	} while (!atomic_compare_exchange(&comm_free_bits, old_value, new_value));

	*pcom = idx;
	return 0;
}

static void
free_pcom(uint32_t pcom)
{
	uint64_t	old_value;
	uint64_t	new_value;

	do {
		old_value = comm_free_bits;
		new_value = old_value | (1ULL << pcom);
	} while (!atomic_compare_exchange(&comm_free_bits, old_value, new_value));
}

int
par_comm_split(uint32_t pcom, int color, int key, uint32_t *new_pcom)
{
	MPI_Comm	comm = {0};
	int		rc = pcom2comm(pcom, &comm);

	if (rc != 0)
		return rc;

	rc = alloc_pcom(new_pcom);
	if (rc != 0)
		return -1;

	rc = MPI_Comm_split(comm, color, key, &comm_table[*new_pcom]);
	if (rc != MPI_SUCCESS) {
		free_pcom(*new_pcom);
		return -1;
	}

	return 0;
}

int
par_comm_free(uint32_t pcom)
{
	MPI_Comm	comm = {0};
	int		rc = pcom2comm(pcom, &comm);

	if (rc != 0)
		return rc;

	rc = MPI_Comm_free(&comm);
	if (rc != MPI_SUCCESS)
		return -1;

	free_pcom(pcom);

	return 0;
}
