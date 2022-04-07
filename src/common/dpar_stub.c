#include <stdio.h>
#include <errno.h>
#include <stdbool.h>
#include <pthread.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <string.h>
#include <daos/dpar.h>

struct par_stubs {
	uint32_t	(*ps_getversion)(void);
	int		(*ps_init)(int *argc, char ***argv);
	int		(*ps_fini)(void);
	int		(*ps_barrier)(uint32_t);
	int		(*ps_rank)(uint32_t comm, int *rank);
	int		(*ps_size)(uint32_t comm, int *size);
	int		(*ps_reduce)(uint32_t comm, const void *sendbuf, void *recvbuf, int count,
				     enum par_type type, enum par_op op, int root);
	int		(*ps_gather)(uint32_t comm, const void *sendbuf, void *recvbuf, int count,
				     enum par_type type, int root);
	int		(*ps_allreduce)(uint32_t comm, const void *sendbuf, void *recvbuf,
					int count, enum par_type type, enum par_op op);
	int		(*ps_allgather)(uint32_t comm, const void *sendbuf, void *recvbuf,
					int count, enum par_type type);
	int		(*ps_bcast)(uint32_t comm, void *buffer, int count, enum par_type type,
				    int root);
	int		(*ps_comm_split)(uint32_t comm, int color, int key, uint32_t *new_comm);
	int		(*ps_comm_free)(uint32_t comm);
};

static struct par_stubs	 stubs;
static void		*stubs_handle;

#define FOREACH_PAR_SYMBOL(ACTION, arg)	\
	ACTION(getversion, arg)	\
	ACTION(init, arg)		\
	ACTION(fini, arg)		\
	ACTION(barrier, arg)		\
	ACTION(rank, arg)		\
	ACTION(size, arg)		\
	ACTION(reduce, arg)		\
	ACTION(gather, arg)		\
	ACTION(allreduce, arg)		\
	ACTION(allgather, arg)		\
	ACTION(bcast, arg)		\
	ACTION(comm_split, arg)		\
	ACTION(comm_free, arg)

#define LOAD_SYM(name, fail)							\
	do {									\
		stubs.ps_##name = dlsym(stubs_handle, "par_" #name);		\
		if (stubs.ps_##name == NULL) {					\
			printf("No par_" #name " found in libdpar_mpi.so\n");	\
			(fail) = true;						\
		}								\
	} while (0);


static pthread_once_t init_control = PTHREAD_ONCE_INIT;

static void
init_routine(void)
{
	bool		fail = false;
	uint32_t	version;

	stubs_handle = dlopen("libdpar_mpi.so", RTLD_NOW);

	if (stubs_handle == NULL) {
		printf("No MPI found, using serial library\n");
		return;
	}

	FOREACH_PAR_SYMBOL(LOAD_SYM, fail);

	if (!fail) {
		version = stubs.ps_getversion();
		if (par_version_compatible(version)) {
			printf("Using compatible version\n");
			return;
		}

		printf("libdpar_mpi.so version %d.%d is not compatible with stub version %d.%d\n",
		       version >> DPAR_VERSION_SHIFT, version & DPAR_VERSION_MASK,
		       DPAR_MAJOR, DPAR_MINOR);
		printf("Continuing with serial library\n");
		fail = true;
	}

	if (fail) {
		/* Ideally, we would do some check here to ensure we are not running under MPI
		 * but I don't know of a reliable way, MPI vendor independent way to do that.
		 */
		memset(&stubs, 0, sizeof(stubs));
	}
}

static void
load_stubs(void)
{
	int	rc;

	rc = pthread_once(&init_control, init_routine);

	if (rc != 0)
		printf("Failure to run execute init_routine: %d: %s\n", rc, strerror(errno));
}

static void __attribute__((destructor))
fini_routine(void)
{
	if (stubs_handle != NULL)
		dlclose(stubs_handle);
}

static void
unload_stubs(void)
{
	memset(&stubs, 0, sizeof(stubs));
}

uint32_t
par_getversion(void)
{
	return DPAR_VERSION;
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
	int	rc;

	if (stubs.ps_fini)
		rc = stubs.ps_fini();

	unload_stubs();

	return rc;
}

int
par_barrier(uint32_t comm)
{
	if (stubs.ps_barrier)
		return stubs.ps_barrier(comm);

	return 0;
}

int
par_rank(uint32_t comm, int *rank)
{
	if (stubs.ps_rank)
		return stubs.ps_rank(comm, rank);

	*rank = 0;

	return 0;
}

int
par_size(uint32_t comm, int *size)
{
	if (stubs.ps_size)
		return stubs.ps_size(comm, size);

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
par_reduce(uint32_t comm, const void *sendbuf, void *recvbuf, int count, enum par_type type,
	   enum par_op op, int root)
{
	ssize_t	size;

	if (stubs.ps_reduce)
		return stubs.ps_reduce(comm, sendbuf, recvbuf, count, type, op, root);

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
par_gather(uint32_t comm, const void *sendbuf, void *recvbuf, int count, enum par_type type,
	   int root)
{
	ssize_t	size;

	if (stubs.ps_gather)
		return stubs.ps_gather(comm, sendbuf, recvbuf, count, type, root);

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
par_allreduce(uint32_t comm, const void *sendbuf, void *recvbuf, int count, enum par_type type,
	      enum par_op op)
{
	ssize_t	size;

	if (stubs.ps_allreduce)
		return stubs.ps_allreduce(comm, sendbuf, recvbuf, count, type, op);

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
par_allgather(uint32_t comm, const void *sendbuf, void *recvbuf, int count, enum par_type type)
{
	ssize_t	size;

	if (stubs.ps_allgather)
		return stubs.ps_allgather(comm, sendbuf, recvbuf, count, type);

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
par_bcast(uint32_t comm, void *buffer, int count, enum par_type type, int root)
{
	if (stubs.ps_bcast)
		return stubs.ps_bcast(comm, buffer, count, type, root);

	return 0;
}

/** Split a communicator to create a new one */
int
par_comm_split(uint32_t comm, int color, int key, uint32_t *new_comm)
{
	if (stubs.ps_comm_split)
		return stubs.ps_comm_split(comm, color, key, new_comm);

	*new_comm = comm;

	return 0;
}

/** Free a communicator */
int
par_comm_free(uint32_t comm)
{
	if (stubs.ps_comm_split)
		return stubs.ps_comm_free(comm);

	return 0;
}
