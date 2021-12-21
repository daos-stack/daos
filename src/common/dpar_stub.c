#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <string.h>
#include <daos/dpar.h>

static pthread_once_t once_control = PTHREAD_ONCE_INIT;

struct par_stubs {
	int	(*ps_init)(int *argc, char ***argv);
	int	(*ps_fini)(void);
	int	(*ps_barrier)(void);
	int	(*ps_rank)(int *rank);
	int	(*ps_size)(int *size);
	int	(*ps_reduce)(const void *sendbuf, void *recvbuf, int count, enum par_type type,
			      enum par_op op, int root);
	int	(*ps_gather)(const void *sendbuf, void *recvbuf, int count, enum par_type type,
			     int root);
	int	(*ps_allreduce)(const void *sendbuf, void *recvbuf, int count, enum par_type type,
				enum par_op op);
	int	(*ps_allgather)(const void *sendbuf, void *recvbuf, int count, enum par_type type);
	int	(*ps_bcast)(void *buffer, int count, enum par_type type, int root);
};

static struct par_stubs	 stubs;
static void		*stubs_handle;

#define FOREACH_PAR_SYMBOL(ACTION, arg)	\
	ACTION(init, arg)		\
	ACTION(fini, arg)		\
	ACTION(barrier, arg)		\
	ACTION(rank, arg)		\
	ACTION(size, arg)		\
	ACTION(reduce, arg)		\
	ACTION(gather, arg)		\
	ACTION(allreduce, arg)		\
	ACTION(allgather, arg)		\
	ACTION(bcast, arg)

#define LOAD_SYM(name, fail)							\
	do {									\
		stubs.ps_##name = dlsym(stubs_handle, "par_" #name);		\
		if (stubs.ps_##name == NULL) {					\
			printf("No par_" #name " found in libdpar_mpi.so\n");	\
			(fail) = true;						\
		}								\
	} while (0);


static void
init_routine(void)
{
	bool	 fail = false;

	stubs_handle = dlopen("libdpar_mpi.so", RTLD_LAZY);

	if (stubs_handle == NULL) {
		printf("No MPI found, using serial library\n");
		return;
	}

	FOREACH_PAR_SYMBOL(LOAD_SYM, fail);

	if (fail)
		memset(&stubs, 0, sizeof(stubs));
}

static void
load_stubs(void)
{
	int	rc;

	rc = pthread_once(&once_control, init_routine);
}

static void __attribute__((destructor))
shutdown(void)
{
	if (stubs_handle != NULL)
		dlclose(stubs_handle);
}

int
par_init(int *argc, char ***argv)
{
	load_stubs();

	if (stubs.ps_init)
		return stubs.ps_init(argc, argv);

	return 0;
}

int
par_fini(void)
{
	load_stubs();

	if (stubs.ps_fini)
		return stubs.ps_fini();

	return 0;
}

int
par_barrier(void)
{
	load_stubs();

	if (stubs.ps_barrier)
		return stubs.ps_barrier();

	return 0;
}

int
par_rank(int *rank)
{
	load_stubs();

	if (stubs.ps_rank)
		return stubs.ps_rank(rank);

	*rank = 0;

	return 0;
}

int
par_size(int *size)
{
	load_stubs();

	if (stubs.ps_size)
		return stubs.ps_size(size);

	*size = 1;

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
	case PAR_UINT64:
		return sizeof(uint64_t);
	default:
		return -1;
	}
}

int
par_reduce(const void *sendbuf, void *recvbuf, int count, enum par_type type, enum par_op op,
	   int root)
{
	ssize_t	size;

	load_stubs();

	if (stubs.ps_reduce)
		return stubs.ps_reduce(sendbuf, recvbuf, count, type, op, root);

	size = type2size(type);

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
	ssize_t	size;

	load_stubs();

	if (stubs.ps_gather)
		return stubs.ps_gather(sendbuf, recvbuf, count, type, root);

	size = type2size(type);

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
	ssize_t	size;

	load_stubs();

	if (stubs.ps_allreduce)
		return stubs.ps_allreduce(sendbuf, recvbuf, count, type, op);

	size = type2size(type);

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
	ssize_t	size;

	load_stubs();

	if (stubs.ps_allgather)
		return stubs.ps_allgather(sendbuf, recvbuf, count, type);

	size = type2size(type);

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
	load_stubs();

	if (stubs.ps_bcast)
		return stubs.ps_bcast(buffer, count, type, root);

	return 0;
}
