#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <daos/dpar.h>

int
par_init(int *argc, char ***argv)
{
	return 0;
}

int
par_fini(void)
{
	return 0;
}

int
par_barrier(void)
{
	return 0;
}

int
par_rank(int *rank)
{
	*rank = 0;

	return 0;
}

int
par_size(int *size)
{
	*size = 0;

	return 0;
}

static inline ssize_t
type2size(enum par_type type)
{
	switch (type) {
	case PAR_BYTE:
		return sizeof(uint8_t);
	case PAR_CHAR:
		return sizeof(char);
	case PAR_DOUBLE:
		return sizeof(double);
	case PAR_INT:
		return sizeof(int);
	default:
		return -1;
	}
}

int
par_reduce(const void *sendbuf, void *recvbuf, int count, enum par_type type, enum par_op op,
	   int root)
{
	ssize_t	size = type2size(type);

	if (size == -1) {
		fprintf(stderr, "Unrecognized type passed to par_reduce %d\n", type);
		return -1;
	}

	memcpy(recvbuf, sendbuf, count * size);

	return 0;
}

/** Gather from all ranks */
int
par_gather(const void *sendbuf, void *recvbuf, int count, enum par_type type,
	   int root)
{
	ssize_t	size = type2size(type);

	if (size == -1) {
		fprintf(stderr, "Unrecognized type passed to par_gather %d\n", type);
		return -1;
	}

	memcpy(recvbuf, sendbuf, count * size);

	return 0;
}

/** All reduce from all ranks */
int
par_allreduce(const void *sendbuf, void *recvbuf, int count, enum par_type type, enum par_op op)
{
	ssize_t	size = type2size(type);

	if (size == -1) {
		fprintf(stderr, "Unrecognized type passed to par_allreduce %d\n", type);
		return -1;
	}

	memcpy(recvbuf, sendbuf, count * size);

	return 0;
}

/** All gather from all ranks */
int
par_allgather(const void *sendbuf, void *recvbuf, int count, enum par_type type)
{
	ssize_t	size = type2size(type);

	if (size == -1) {
		fprintf(stderr, "Unrecognized type passed to par_allgather %d\n", type);
		return -1;
	}

	memcpy(recvbuf, sendbuf, count * size);

	return 0;
}

/** Broadcast to all ranks */
int
par_bcast(void *buffer, int count, enum par_type type, int root)
{
	return 0;
}
