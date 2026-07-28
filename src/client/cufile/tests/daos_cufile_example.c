/**
 * (C) Copyright 2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * \file
 * Example: GPU Direct Storage on DAOS via dfuse (Mode 1 — fd-based)
 *
 * Demonstrates a full write-read-verify cycle using standard cuFile APIs
 * with DAOS as the backend. The file is opened via dfuse (standard POSIX),
 * and only 2 lines differ from a kernel-GDS (Lustre/WekaFS) application:
 *   1. desc.type    = CU_FILE_HANDLE_TYPE_USERSPACE_FS;
 *   2. desc.fs_ops  = (CUfileFSOps_t *)daos_cufile_ops;
 *
 * Data flows directly from GPU ↔ DAOS servers via DFS, bypassing FUSE
 * for I/O while still using dfuse for file namespace (open/close/stat).
 *
 * Build:
 *   gcc -o daos_cufile_example daos_cufile_example.c \
 *       -ldaos_cufile -ldaos -ldfs -lcufile -lcuda -lcudart
 *
 * Run (requires dfuse mounted, e.g. at /mnt/daos):
 *   ./daos_cufile_example /mnt/daos/test_file.bin
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

/* cuFile header (from CUDA Toolkit) */
#include <cufile.h>

/* CUDA runtime for GPU memory allocation */
#include <cuda_runtime.h>

/* DAOS cuFile plugin — provides daos_cufile_ops for fd-based registration */
#include <daos_cufile.h>

#define BUF_SIZE (64 * 1024 * 1024) /* 64 MB */

/**
 * Fill GPU buffer with a known pattern for verification.
 * Pattern: each 4-byte word = its byte offset / 4.
 */
static int
fill_pattern(void *gpu_buf, size_t size)
{
	unsigned int *host_buf;
	size_t        i;

	host_buf = (unsigned int *)malloc(size);
	if (host_buf == NULL)
		return -1;

	for (i = 0; i < size / sizeof(unsigned int); i++)
		host_buf[i] = (unsigned int)i;

	if (cudaMemcpy(gpu_buf, host_buf, size, cudaMemcpyHostToDevice) != cudaSuccess) {
		free(host_buf);
		return -1;
	}

	free(host_buf);
	return 0;
}

/**
 * Verify GPU buffer contains the expected pattern.
 * Returns 0 on success, -1 on mismatch.
 */
static int
verify_pattern(void *gpu_buf, size_t size)
{
	unsigned int *host_buf;
	size_t        i;
	int           rc = 0;

	host_buf = (unsigned int *)malloc(size);
	if (host_buf == NULL)
		return -1;

	if (cudaMemcpy(host_buf, gpu_buf, size, cudaMemcpyDeviceToHost) != cudaSuccess) {
		free(host_buf);
		return -1;
	}

	for (i = 0; i < size / sizeof(unsigned int); i++) {
		if (host_buf[i] != (unsigned int)i) {
			fprintf(stderr, "  MISMATCH at word %zu: expected %u, got %u\n", i,
				(unsigned int)i, host_buf[i]);
			rc = -1;
			break;
		}
	}

	free(host_buf);
	return rc;
}

int
main(int argc, char *argv[])
{
	const char    *file_path;
	CUfileDescr_t  desc;
	CUfileHandle_t cfh_wr;
	CUfileHandle_t cfh_rd;
	CUfileError_t  cf_err;
	void          *gpu_buf_wr = NULL;
	void          *gpu_buf_rd = NULL;
	ssize_t        ret;
	int            fd_wr = -1;
	int            fd_rd = -1;
	int            exit_code = 0;

	if (argc < 2) {
		fprintf(stderr,
			"Usage: %s <path_on_dfuse>\n"
			"  e.g.: %s /mnt/daos/test_file.bin\n"
			"  Requires dfuse mounted.\n",
			argv[0], argv[0]);
		return 1;
	}
	file_path = argv[1];

	/* 1. Initialize cuFile driver */
	cf_err = cuFileDriverOpen();
	if (cf_err.err != CU_FILE_SUCCESS) {
		fprintf(stderr, "cuFileDriverOpen failed: %d\n", cf_err.err);
		return 1;
	}

	/* 2. Allocate and register GPU memory */
	if (cudaMalloc(&gpu_buf_wr, BUF_SIZE) != cudaSuccess ||
	    cudaMalloc(&gpu_buf_rd, BUF_SIZE) != cudaSuccess) {
		fprintf(stderr, "cudaMalloc failed\n");
		exit_code = 1;
		goto out_gpu;
	}

	cf_err = cuFileBufRegister(gpu_buf_wr, BUF_SIZE, 0);
	if (cf_err.err != CU_FILE_SUCCESS) {
		fprintf(stderr, "cuFileBufRegister(wr) failed: %d\n", cf_err.err);
		exit_code = 1;
		goto out_gpu;
	}
	cf_err = cuFileBufRegister(gpu_buf_rd, BUF_SIZE, 0);
	if (cf_err.err != CU_FILE_SUCCESS) {
		fprintf(stderr, "cuFileBufRegister(rd) failed: %d\n", cf_err.err);
		exit_code = 1;
		goto out_bufreg_wr;
	}

	/* 3. Fill write buffer with known pattern */
	if (fill_pattern(gpu_buf_wr, BUF_SIZE) != 0) {
		fprintf(stderr, "fill_pattern failed\n");
		exit_code = 1;
		goto out_bufreg_rd;
	}

	/*
	 * === WRITE: GPU → DAOS via cuFileWrite ===
	 *
	 * Standard POSIX open on dfuse mount, then register with cuFile
	 * using USERSPACE_FS type and DAOS plugin ops.
	 *
	 * Compare to kernel-GDS (Lustre/WekaFS):
	 *   desc.type      = CU_FILE_HANDLE_TYPE_OPAQUE_FD;   ← kernel path
	 *   // no fs_ops needed
	 *
	 * DAOS (only 2 lines differ):
	 *   desc.type      = CU_FILE_HANDLE_TYPE_USERSPACE_FS; ← userspace path
	 *   desc.fs_ops    = (CUfileFSOps_t *)daos_cufile_ops; ← DAOS plugin
	 */
	fd_wr = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd_wr < 0) {
		perror("open(write)");
		exit_code = 1;
		goto out_bufreg_rd;
	}

	memset(&desc, 0, sizeof(desc));
	desc.type      = CU_FILE_HANDLE_TYPE_USERSPACE_FS;
	desc.handle.fd = fd_wr;
	desc.fs_ops    = (CUfileFSOps_t *)daos_cufile_ops;

	cf_err = cuFileHandleRegister(&cfh_wr, &desc);
	if (cf_err.err != CU_FILE_SUCCESS) {
		fprintf(stderr, "cuFileHandleRegister(wr) failed: %d\n", cf_err.err);
		exit_code = 1;
		goto out_fd_wr;
	}

	printf("cuFileWrite: %d MB GPU → DAOS:%s\n", BUF_SIZE / (1024 * 1024), file_path);

	ret = cuFileWrite(cfh_wr, gpu_buf_wr, BUF_SIZE, 0, 0);
	if (ret < 0) {
		fprintf(stderr, "cuFileWrite failed: %zd\n", ret);
		exit_code = 1;
	} else {
		printf("  Written %zd bytes.\n", ret);
	}

	cuFileHandleDeregister(cfh_wr);
	close(fd_wr);
	fd_wr = -1;

	if (exit_code != 0)
		goto out_bufreg_rd;

	/*
	 * === READ: DAOS → GPU via cuFileRead ===
	 */
	fd_rd = open(file_path, O_RDONLY);
	if (fd_rd < 0) {
		perror("open(read)");
		exit_code = 1;
		goto out_bufreg_rd;
	}

	memset(&desc, 0, sizeof(desc));
	desc.type      = CU_FILE_HANDLE_TYPE_USERSPACE_FS;
	desc.handle.fd = fd_rd;
	desc.fs_ops    = (CUfileFSOps_t *)daos_cufile_ops;

	cf_err = cuFileHandleRegister(&cfh_rd, &desc);
	if (cf_err.err != CU_FILE_SUCCESS) {
		fprintf(stderr, "cuFileHandleRegister(rd) failed: %d\n", cf_err.err);
		exit_code = 1;
		goto out_fd_rd;
	}

	printf("cuFileRead:  %d MB DAOS:%s → GPU\n", BUF_SIZE / (1024 * 1024), file_path);

	ret = cuFileRead(cfh_rd, gpu_buf_rd, BUF_SIZE, 0, 0);
	if (ret < 0) {
		fprintf(stderr, "cuFileRead failed: %zd\n", ret);
		exit_code = 1;
		goto out_rd_cufile;
	}
	printf("  Read %zd bytes.\n", ret);

	/*
	 * === VERIFY ===
	 */
	printf("Verifying %zd bytes...\n", ret);
	if (verify_pattern(gpu_buf_rd, (size_t)ret) == 0)
		printf("  PASSED — data integrity verified!\n");
	else {
		fprintf(stderr, "  FAILED — data corruption detected!\n");
		exit_code = 1;
	}

out_rd_cufile:
	cuFileHandleDeregister(cfh_rd);
out_fd_rd:
	if (fd_rd >= 0)
		close(fd_rd);
	goto out_bufreg_rd;

out_fd_wr:
	if (fd_wr >= 0)
		close(fd_wr);

out_bufreg_rd:
	cuFileBufDeregister(gpu_buf_rd);
out_bufreg_wr:
	cuFileBufDeregister(gpu_buf_wr);
out_gpu:
	if (gpu_buf_rd)
		cudaFree(gpu_buf_rd);
	if (gpu_buf_wr)
		cudaFree(gpu_buf_wr);
	cuFileDriverClose();

	return exit_code;
}
