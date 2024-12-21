/**
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <sys/syscall.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

#include <gurt/atomic.h>
#include <gurt/common.h>
#include <gurt/shm_alloc.h>
#include <gurt/shm_utils.h>

/* the name of shared memory used for mmap which will be found under /dev/shm/ */
#define daos_shm_name "daos_shm_cache"

/* pid of current process */
static int          pid;

/* the memory allocator that will be used to handle small memory allocation */
static __thread int idx_small = -1;

/* the address of shared memory region */
struct d_shm_hdr   *d_shm_head;

/* the attribute set for mutex located inside shared memory */
pthread_mutexattr_t d_shm_mutex_attr;

/* the number of times to try calling shm_open() */
#define RETRY (5)

/**
 * pid of the process who creates shared memory region. shared memory is NOT unmapped when this
 * process exits to keep shm always available. shared memory is unmapped when other processes
 * exit.
 */
static int          pid_shm_creator;

int
shm_init(void)
{
	int      i;
	int      shm_ht_fd;
	int      shmopen_perm = 0600;
	void    *shm_addr;
	int      rc;
	char     daos_shm_name_buf[64];
	uint64_t shm_size;
	uint64_t shm_pool_size;

	if (pid == 0)
		pid = getpid();
	if (d_shm_head) {
		while (d_shm_head->magic != DSM_MAGIC)
			usleep(1);
		/* shared memory already initlized in current process */
		return 0;
	}

	rc = d_getenv_uint64_t("DAOS_SHM_SIZE", &shm_size);
	if (rc != -DER_NONEXIST) {
		/* set parameter from env */
		shm_pool_size = shm_size / N_SHM_POOL;
		if (shm_pool_size % 4096)
			/* make shm_pool_size 4K aligned */
			shm_pool_size += (4096 - (shm_pool_size % 4096));
		shm_size = shm_pool_size * N_SHM_POOL + sizeof(struct d_shm_hdr);
	} else {
		shm_pool_size = SHM_POOL_SIZE;
		shm_size      = SHM_SIZE_REQ;
	}

	rc = pthread_mutexattr_init(&d_shm_mutex_attr);
	D_ASSERT(rc == 0);
	rc = pthread_mutexattr_settype(&d_shm_mutex_attr, PTHREAD_MUTEX_NORMAL);
	D_ASSERT(rc == 0);
	rc = pthread_mutexattr_setpshared(&d_shm_mutex_attr, PTHREAD_PROCESS_SHARED);
	D_ASSERT(rc == 0);
	pthread_mutexattr_setrobust(&d_shm_mutex_attr, PTHREAD_MUTEX_ROBUST);
	D_ASSERT(rc == 0);

	/* the shared memory only accessible for individual user for now */
	sprintf(daos_shm_name_buf, "%s_%d", daos_shm_name, getuid());
open_rw:
	shm_ht_fd = shm_open(daos_shm_name_buf, O_RDWR, shmopen_perm);
	/* failed to open */
	if (shm_ht_fd == -1) {
		if (errno == ENOENT) {
			goto create_shm;
		} else {
			DS_ERROR(errno, "unexpected error shm_open()");
			for (i = 0; i < RETRY; i++) {
				/* take a short nap to wait for the creation of shm */
				usleep(10);
				shm_ht_fd = shm_open(daos_shm_name_buf, O_RDWR, shmopen_perm);
				if (shm_ht_fd >= 0)
					break;
			}
			if (i >= RETRY) {
				DS_ERROR(errno, "failed to open shared memory after %d retries",
					 RETRY);
				goto err;
			}
		}
	}

	/* map existing shared memory */
	shm_addr = mmap(FIXED_SHM_ADDR, shm_size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_FIXED, shm_ht_fd, 0);
	if (shm_addr != FIXED_SHM_ADDR) {
		DS_ERROR(errno, "mmap failed to map at desired address");
		goto err;
	}
	/* wait until the shared memory initialization finished */
	while (((struct d_shm_hdr *)shm_addr)->magic != DSM_MAGIC)
		usleep(1);
	if (((struct d_shm_hdr *)shm_addr)->size != shm_size) {
		/* EBADRQC - Invalid request code */
		errno = EBADRQC;
		DS_ERROR(errno, "unexpected shared memory size. Multiple versions of daos or env?");
		goto err_unmap;
	}
	atomic_fetch_add_relaxed(&(((struct d_shm_hdr *)shm_addr)->ref_count), 1);
	d_shm_head = (struct d_shm_hdr *)shm_addr;
	close(shm_ht_fd);
	return 0;

create_shm:
	shm_ht_fd = shm_open(daos_shm_name_buf, O_RDWR | O_CREAT | O_EXCL, shmopen_perm);
	/* failed to create */
	if (shm_ht_fd == -1) {
		if (errno == EEXIST)
			goto open_rw;
		DS_ERROR(errno, "shm_open() failed to create shared memory");
		return errno;
	}
	/* set the size of shared memory region */
	if (ftruncate(shm_ht_fd, shm_size) != 0) {
		DS_ERROR(errno, "ftruncate() failed for shm_ht_fd");
		goto err;
	}
	/* map the shared memory at a fixed address for now. We will remove this limit later. */
	shm_addr = mmap(FIXED_SHM_ADDR, shm_size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_FIXED, shm_ht_fd, 0);
	if (shm_addr != FIXED_SHM_ADDR) {
		DS_ERROR(errno, "mmap() failed to map at desired address");
		goto err;
	}
	d_shm_head        = (struct d_shm_hdr *)shm_addr;
	d_shm_head->magic = 0;
	/* initialize memory allocators */
	for (i = 0; i < N_SHM_POOL; i++) {
		d_shm_head->tlsf[i] = tlsf_create_with_pool(
			shm_addr + sizeof(struct d_shm_hdr) + (i * shm_pool_size), shm_pool_size);
	}

	if (shm_mutex_init(&(d_shm_head->g_lock)) != 0) {
		DS_ERROR(errno, "shm_mutex_init() failed");
		goto err_unmap;
	}
	for (i = 0; i < N_SHM_POOL; i++) {
		if (shm_mutex_init(&(d_shm_head->mem_lock[i])) != 0) {
			DS_ERROR(errno, "shm_mutex_init() failed");
			goto err_unmap;
		}
	}
	if (shm_mutex_init(&(d_shm_head->ht_lock)) != 0) {
		DS_ERROR(errno, "shm_mutex_init() failed");
		goto err_unmap;
	}

	pid_shm_creator         = pid;
	d_shm_head->off_ht_head = INVALID_OFFSET;

	atomic_store_relaxed(&(d_shm_head->ref_count), 1);
	atomic_store_relaxed(&(d_shm_head->large_mem_count), 0);
	d_shm_head->size           = shm_size;
	d_shm_head->shm_pool_size  = shm_pool_size;
	d_shm_head->magic          = DSM_MAGIC;
	/* initialization is finished now. */
	return 0;

err_unmap:
	d_shm_head = NULL;
	munmap(shm_addr, shm_size);

err:
	close(shm_ht_fd);
	return errno;
}

static void *
shm_alloc_comm(size_t align, size_t size)
{
	int      idx_allocator;
	void    *buf;
	int      tid;
	uint32_t hash;
	uint64_t oldref;

	if (idx_small < 0) {
		tid  = syscall(SYS_gettid);
		hash = d_hash_string_u32((const char *)&tid, sizeof(int));
		/* choose a memory allocator based on tid */
		idx_small = hash % N_SHM_POOL;
	}
	idx_allocator = idx_small;
	if (size >= LARGE_MEM) {
		oldref = atomic_fetch_add_relaxed(&(d_shm_head->large_mem_count), 1);
		/* pick the allocator for large memory request with round-robin */
		idx_allocator = oldref % N_SHM_POOL;
	}
	shm_mutex_lock(&(d_shm_head->mem_lock[idx_allocator]), NULL);
	if (align == 0)
		buf = tlsf_malloc(d_shm_head->tlsf[idx_allocator], size);
	else
		buf = tlsf_memalign(d_shm_head->tlsf[idx_allocator], align, size);
	shm_mutex_unlock(&(d_shm_head->mem_lock[idx_allocator]));

	return buf;
}

void *
shm_alloc(size_t size)
{
	return shm_alloc_comm(0, size);
}

void *
shm_memalign(size_t align, size_t size)
{
	return shm_alloc_comm(align, size);
}

void
shm_free(void *ptr)
{
	uint32_t idx_allocator;

	/* compare with the lower bound address of shared memory pool */
	if (ptr < d_shm_head->tlsf[0]) {
		DS_ERROR(EINVAL, "Out of range memory pointer for shm_free()\n");
		return;
	}

	idx_allocator = ((uint64_t)ptr - (uint64_t)d_shm_head->tlsf[0]) / d_shm_head->shm_pool_size;
	/* compare with the upper bound address of shared memory pool */
	if (idx_allocator >= N_SHM_POOL) {
		DS_ERROR(EINVAL, "Out of range memory pointer for shm_free()\n");
		return;
	}

	shm_mutex_lock(&(d_shm_head->mem_lock[idx_allocator]), NULL);
	tlsf_free(d_shm_head->tlsf[idx_allocator], ptr);
	shm_mutex_unlock(&(d_shm_head->mem_lock[idx_allocator]));
}

void
shm_destroy(void)
{
	char daos_shm_file_name[128];

	sprintf(daos_shm_file_name, "/dev/shm/%s_%d", daos_shm_name, getuid());
	unlink(daos_shm_file_name);
}

bool
shm_inited(void)
{
	if (d_shm_head == NULL)
		return false;
	if (d_shm_head->magic != DSM_MAGIC)
		return false;

	return true;
}

void
shm_fini(void)
{
	if (!shm_inited())
		return;

	atomic_fetch_add_relaxed(&(d_shm_head->ref_count), -1);
	if (pid != pid_shm_creator)
		munmap(d_shm_head, d_shm_head->size);
	d_shm_head = NULL;
}
