/**
 * (C) Copyright 2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * \file
 * Example: GPU Direct Storage on DAOS via Mode 2 (explicit DAOS API)
 *
 * Demonstrates cuFile usage without dfuse — the application connects to
 * DAOS pool/container explicitly using daos_cufile_register(), which
 * handles all setup internally. No dfuse mount required.
 *
 * Build:
 *   gcc -o daos_cufile_mode2_example daos_cufile_mode2_example.c \
 *       -ldaos_cufile -ldaos -ldfs -lcufile -lcuda -lcudart
 *
 * Run (no dfuse needed, uses env vars):
 *   export DAOS_POOL=mypool
 *   export DAOS_CONT=mycont
 *   ./daos_cufile_mode2_example /data/test_file.bin
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

/* cuFile header (from CUDA Toolkit) */
#include <cufile.h>

/* CUDA runtime for GPU memory allocation */
#include <cuda_runtime.h>

/* DAOS cuFile plugin */
#include <daos_cufile.h>

#define BUF_SIZE (64 * 1024 * 1024) /* 64 MB */

/**
 * Fill GPU buffer with a known pattern for verification.
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
	const char        *file_path;
	daos_cufile_reg_t *reg_wr = NULL;
	daos_cufile_reg_t *reg_rd = NULL;
	CUfileHandle_t     cfh;
	CUfileError_t      cf_err;
	void              *gpu_buf_wr = NULL;
	void              *gpu_buf_rd = NULL;
	ssize_t            ret;
	int                rc;
	int                exit_code = 0;

	if (argc < 2) {
		fprintf(stderr,
			"Usage: %s <file_path>\n"
			"  e.g.: %s /data/test_file.bin\n"
			"  Requires DAOS_POOL and DAOS_CONT env vars.\n"
			"  No dfuse mount needed.\n",
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
	 * === WRITE: GPU → DAOS via cuFileWrite (Mode 2) ===
	 *
	 * daos_cufile_register() does all DAOS setup in one call:
	 *   - connects to pool/container (from DAOS_POOL/DAOS_CONT env vars)
	 *   - opens the file in the DFS namespace
	 *   - prepares the cuFile descriptor
	 *
	 * No dfuse mount needed. Pool/container can also be passed explicitly:
	 *   daos_cufile_register(path, flags, "mypool", "mycont", &cfh, &reg);
	 */
	rc = daos_cufile_register(file_path, O_WRONLY | O_CREAT | O_TRUNC, NULL, NULL, &cfh,
				  &reg_wr);
	if (rc != 0) {
		fprintf(stderr, "daos_cufile_register(write) failed: %d\n", rc);
		exit_code = 1;
		goto out_bufreg_rd;
	}

	cf_err = cuFileHandleRegister(&cfh, daos_cufile_get_desc(reg_wr));
	if (cf_err.err != CU_FILE_SUCCESS) {
		fprintf(stderr, "cuFileHandleRegister(wr) failed: %d\n", cf_err.err);
		exit_code = 1;
		goto out_reg_wr;
	}

	printf("cuFileWrite: %d MB GPU → DAOS:%s\n", BUF_SIZE / (1024 * 1024), file_path);

	ret = cuFileWrite(cfh, gpu_buf_wr, BUF_SIZE, 0, 0);
	if (ret < 0) {
		fprintf(stderr, "cuFileWrite failed: %zd\n", ret);
		exit_code = 1;
	} else {
		printf("  Written %zd bytes.\n", ret);
	}

	cuFileHandleDeregister(cfh);
	daos_cufile_deregister(reg_wr);
	reg_wr = NULL;

	if (exit_code != 0)
		goto out_bufreg_rd;

	/*
	 * === READ: DAOS → GPU via cuFileRead (Mode 2) ===
	 */
	rc = daos_cufile_register(file_path, O_RDONLY, NULL, NULL, &cfh, &reg_rd);
	if (rc != 0) {
		fprintf(stderr, "daos_cufile_register(read) failed: %d\n", rc);
		exit_code = 1;
		goto out_bufreg_rd;
	}

	cf_err = cuFileHandleRegister(&cfh, daos_cufile_get_desc(reg_rd));
	if (cf_err.err != CU_FILE_SUCCESS) {
		fprintf(stderr, "cuFileHandleRegister(rd) failed: %d\n", cf_err.err);
		exit_code = 1;
		goto out_reg_rd;
	}

	printf("cuFileRead:  %d MB DAOS:%s → GPU\n", BUF_SIZE / (1024 * 1024), file_path);

	ret = cuFileRead(cfh, gpu_buf_rd, BUF_SIZE, 0, 0);
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
	cuFileHandleDeregister(cfh);
out_reg_rd:
	if (reg_rd)
		daos_cufile_deregister(reg_rd);
	goto out_bufreg_rd;

out_reg_wr:
	if (reg_wr)
		daos_cufile_deregister(reg_wr);

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
