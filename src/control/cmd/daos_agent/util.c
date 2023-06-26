/**
 * (C) Copyright 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <libgen.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>

#include "util.h"

static int	shm_fd;
static char	*shm_ptr;
daos_shm_sb	*sb;

int
setup_client_cache(size_t size)
{
	size_t			sb_size = sizeof(daos_shm_sb);
	pthread_mutexattr_t	attr;
	int			rc;

	printf("Starting Client cache\n");

	/** Create and open the shared memory segment */
	shm_fd = shm_open(DAOS_SHM_NAME, O_CREAT | O_RDWR, 0666);
	if (shm_fd == -1) {
		rc = errno;
		perror("shm_open");
		return rc;
	}

	/** Set the size of the shared memory segment */
	if (ftruncate(shm_fd, size) == -1) {
		rc = errno;
		perror("ftruncate");
		goto err_unlink;
	}

	/** Map the shared memory segment into the process's address space */
	sb = (daos_shm_sb *)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	if (sb == MAP_FAILED) {
		rc = errno;
		perror("mmap");
		goto err_unlink;
	}

	rc = pthread_mutexattr_init(&attr);
	if (rc) {
		fprintf(stderr, "Failed pthread_mutexattr_init(): %d\n", rc);
		goto err_mmap;
	}

	rc = pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
	if (rc) {
		fprintf(stderr, "Failed pthread_mutexattr_setrobust(): %d\n", rc);
		pthread_mutexattr_destroy(&attr);
		goto err_mmap;
	}

	rc = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
	if (rc) {
		fprintf(stderr, "Failed pthread_mutexattr_setpshared(): %d\n", rc);
		pthread_mutexattr_destroy(&attr);
		goto err_mmap;
	}

	rc = pthread_mutex_init(&sb->dss_mutex, &attr);
	if (rc) {
		fprintf(stderr, "Failed pthread_mutex_init(): %d\n", rc);
		pthread_mutexattr_destroy(&attr);
		goto err_mmap;
	}

	pthread_mutexattr_destroy(&attr);
	sb->dss_magic = DAOS_SHM_MAGIC;
	printf("Started Client cache\n");
	return 0;

err_mmap:
	munmap(shm_ptr, size);
err_unlink:
	shm_unlink(DAOS_SHM_NAME);
	return rc;
}

int
destroy_client_cache(size_t size)
{
	int rc = 0, rc2;

	printf("Destroying Client cache\n");

	pthread_mutex_destroy(&sb->dss_mutex);

	if (munmap(sb, size) == -1) {
		rc = errno;
		perror("munmap");
	}

	rc2 = close(shm_fd);
	if (rc2) {
		rc2 = errno;
		perror("close");
		if (rc == 0)
			rc = rc2;
	}

	rc2 = shm_unlink(DAOS_SHM_NAME);
	if (rc2) {
		rc2 = errno;
		perror("shm_unlink");
		if (rc == 0)
			rc = rc2;
	}

	return rc;
}
