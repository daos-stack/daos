/*
 * (C) Copyright 2023 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <ctype.h>

#include <criterion/criterion.h>
#include <criterion/parameterized.h>
#include <pthread.h>

#include <cuda.h>
#include <cuda_runtime.h>

#include "libcxi/libcxi.h"
#include "cxip.h"
#include "cxip_test_common.h"

#define MAX_MSG_SIZE 1048576U
#define MAX_BUF_OFFSET 65536U

unsigned int seed;

static void cuda_init(void)
{
	enable_cxi_hmem_ops = 0;
	seed = time(NULL);
	srand(seed);
}

TestSuite(cuda, .timeout = CXIT_DEFAULT_TIMEOUT, .init = cuda_init);

static void cuda_message_runner(void *cuda_send_buf, void *cuda_recv_buf,
				size_t buf_size, bool device_only_mem,
				bool unexpected)
{
	int ret;
	char *send_buf;
	char *recv_buf;
	struct fi_cq_tagged_entry cqe;
	int i;
	cudaError_t cuda_ret;
	int j;

	cxit_setup_msg();

	/* For device only memcpy, send and recv buffer as used for data
	   validation.
	*/
	if (device_only_mem) {
		send_buf = malloc(buf_size);
		cr_assert_neq(send_buf, NULL, "Failed to allocate memory");

		recv_buf = calloc(1, buf_size);
		cr_assert_neq(send_buf, NULL, "Failed to allocate memory");
	} else {
		send_buf = cuda_send_buf;
		recv_buf = cuda_recv_buf;
	}

	for (j = 0; j < 2; j++) {

		ret = open("/dev/urandom", O_RDONLY);
		cr_assert_neq(ret, -1, "open failed: %d", -errno);
		read(ret, send_buf, buf_size);
		close(ret);

		if (device_only_mem) {
			cuda_ret = cudaMemcpy(cuda_send_buf, send_buf, buf_size,
					      cudaMemcpyHostToDevice);
			cr_assert_eq(cuda_ret, cudaSuccess, "cudaMemcpy failed: %d",
				     cuda_ret);
		}


		if (unexpected) {
			ret = fi_send(cxit_ep, cuda_send_buf, buf_size, NULL, cxit_ep_fi_addr,
				      NULL);
			cr_assert_eq(ret, FI_SUCCESS, "fi_send failed: %d", ret);

			ret = fi_recv(cxit_ep, cuda_recv_buf, buf_size, NULL, cxit_ep_fi_addr,
				      NULL);
			cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed: %d", ret);
		} else {
			ret = fi_recv(cxit_ep, cuda_recv_buf, buf_size, NULL, cxit_ep_fi_addr,
				      NULL);
			cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed: %d", ret);

			ret = fi_send(cxit_ep, cuda_send_buf, buf_size, NULL, cxit_ep_fi_addr,
				      NULL);
			cr_assert_eq(ret, FI_SUCCESS, "fi_send failed: %d", ret);
		}

		do {
			ret = fi_cq_read(cxit_rx_cq, &cqe, 1);
		} while (ret == -FI_EAGAIN);
		cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

		do {
			ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
		} while (ret == -FI_EAGAIN);
		cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

		if (device_only_mem) {
			cuda_ret = cudaMemcpy(recv_buf, cuda_recv_buf, buf_size,
					      cudaMemcpyDeviceToHost);
			cr_assert_eq(cuda_ret, cudaSuccess, "cudaMemcpy failed: %d",
				     cuda_ret);
		}

		for (i = 0; i < buf_size; i++)
			cr_assert_eq(send_buf[i], recv_buf[i],
				     "Data corruption at byte %d seed %u iter %d", i, seed, j);
	}

	if (device_only_mem) {
		free(recv_buf);
		free(send_buf);
	}

	cxit_teardown_msg();
}

static void cuda_dev_memory_test(size_t buf_size, size_t buf_offset,
				 bool unexpected, bool hmem_dev_reg)
{
	cudaError_t cuda_ret;
	void *cuda_send_buf;
	void *cuda_recv_buf;
	int ret;

	if (hmem_dev_reg)
		ret = setenv("FI_CXI_DISABLE_HMEM_DEV_REGISTER", "0", 1);
	else
		ret = setenv("FI_CXI_DISABLE_HMEM_DEV_REGISTER", "1", 1);
	cr_assert_eq(ret, 0, "setenv failed: %d", -errno);

	/* cuda buffers will be used for RDMA. */
	cuda_ret = cudaMalloc(&cuda_send_buf, buf_size + buf_offset);
	cr_assert_eq(cuda_ret, cudaSuccess, "cudaMalloc failed: %d", cuda_ret);

	cuda_ret = cudaMalloc(&cuda_recv_buf, buf_size + buf_offset);
	cr_assert_eq(cuda_ret, cudaSuccess, "cudaMalloc failed: %d", cuda_ret);

	int attr_value = 1;
	cuPointerSetAttribute(&attr_value, CU_POINTER_ATTRIBUTE_SYNC_MEMOPS, (CUdeviceptr)cuda_send_buf);

	cuda_message_runner((void *)((char *)cuda_send_buf + buf_offset),
			    (void *)((char *)cuda_recv_buf + buf_offset),
			    buf_size, true, unexpected);

	cuda_ret = cudaFree(cuda_recv_buf);
	cr_assert_eq(cuda_ret, cudaSuccess, "cudaFree  failed: %d", cuda_ret);

	cuda_ret = cudaFree(cuda_send_buf);
	cr_assert_eq(cuda_ret, cudaSuccess, "cudaFree  failed: %d", cuda_ret);

}

/* Test messaging using rendezvous, device memory, and HMEM device memory
 * registration for load/store access.
 */
Test(cuda, messaging_devMemory_rdvz_hmemDevReg)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % MAX_MSG_SIZE;
		if (buf_size > 65536)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	cuda_dev_memory_test(buf_size, buf_offset, false, true);
}

/* Test messaging using eager, device memory, and HMEM device memory
 * registration for load/store access.
 */
Test(cuda, messaging_devMemory_eager_hmemDevReg)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % 1024;
		if (buf_size > 256)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	cuda_dev_memory_test(buf_size, buf_offset, false, true);
}

/* Test messaging using IDC, device memory, and HMEM device memory
 * registration for load/store access.
 */
Test(cuda, messaging_devMemory_idc_hmemDevReg)
{
	size_t buf_size;
	size_t buf_offset;

	buf_size = rand() % 128;
	buf_offset = rand() % MAX_BUF_OFFSET;

	cuda_dev_memory_test(buf_size, buf_offset, false, true);
}

/* Test messaging using rendezvous, device memory, unexpected messaging, and
 * HMEM device memory registration for load/store access.
 */
Test(cuda, messaging_devMemory_rdvz_unexpected_hmemDevReg)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % MAX_MSG_SIZE;
		if (buf_size > 65536)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	cuda_dev_memory_test(buf_size, buf_offset, true, true);
}

/* Test messaging using eager, device memory, unexpected messaging, and
 * HMEM device memory registration for load/store access.
 */
Test(cuda, messaging_devMemory_eager_unexpected_hmemDevReg)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % 1024;
		if (buf_size > 256)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	cuda_dev_memory_test(buf_size, buf_offset, true, true);
}

/* Test messaging using IDC, device memory, unexpected messaging, and
 * HMEM device memory registration for load/store access.
 */
Test(cuda, messaging_devMemory_idc_unexpected_hmemDevReg)
{
	size_t buf_size;
	size_t buf_offset;

	buf_size = rand() % 128;
	buf_offset = rand() % MAX_BUF_OFFSET;

	cuda_dev_memory_test(buf_size, buf_offset, true, true);
}

/* Test messaging using rendezvous, device memory, and without HMEM device memory
 * registration for load/store access.
 */
Test(cuda, messaging_devMemory_rdvz_noHmemDevReg)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % MAX_MSG_SIZE;
		if (buf_size > 65536)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	cuda_dev_memory_test(buf_size, buf_offset, false, false);
}

/* Test messaging using eager, device memory, and without HMEM device memory
 * registration for load/store access.
 */
Test(cuda, messaging_devMemory_eager_noHmemDevReg)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % 1024;
		if (buf_size > 256)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	cuda_dev_memory_test(buf_size, buf_offset, false, false);
}

/* Test messaging using IDC, device memory, and without HMEM device memory
 * registration for load/store access.
 */
Test(cuda, messaging_devMemory_idc_noHmemDevReg)
{
	size_t buf_size;
	size_t buf_offset;

	buf_size = rand() % 128;
	buf_offset = rand() % MAX_BUF_OFFSET;

	cuda_dev_memory_test(buf_size, buf_offset, false, false);
}

/* Test messaging using rendezvous, device memory, unexpected messaging, and
 * without HMEM device memory registration for load/store access.
 */
Test(cuda, messaging_devMemory_rdvz_unexpected_noHmemDevReg)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % MAX_MSG_SIZE;
		if (buf_size > 65536)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	cuda_dev_memory_test(buf_size, buf_offset, true, false);
}

/* Test messaging using eager, device memory, unexpected messaging, and
 * without HMEM device memory registration for load/store access.
 */
Test(cuda, messaging_devMemory_eager_unexpected_noHmemDevReg)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % 1024;
		if (buf_size > 256)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	cuda_dev_memory_test(buf_size, buf_offset, true, false);
}

/* Test messaging using IDC, device memory, unexpected messaging, and
 * without HMEM device memory registration for load/store access.
 */
Test(cuda, messaging_devMemory_idc_unexpected_noHmemDevReg)
{
	size_t buf_size;
	size_t buf_offset;

	buf_size = rand() % 128;
	buf_offset = rand() % MAX_BUF_OFFSET;

	cuda_dev_memory_test(buf_size, buf_offset, true, false);
}

static void verify_dev_reg_handle(bool hmem_dev_reg)
{
	int ret;
	void *buf;
	cudaError_t cuda_ret;
	struct fid_mr *fid_mr;
	size_t buf_size = 1024;
	struct cxip_mr *mr;

	cxit_setup_msg();

	cuda_ret = cudaMalloc(&buf, buf_size);
	cr_assert_eq(cuda_ret, cudaSuccess, "cudaMalloc failed: %d", cuda_ret);

	ret = fi_mr_reg(cxit_domain, buf, buf_size, FI_READ, 0, 0x123, 0,
			&fid_mr, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_reg failed: %d", ret);

	mr = container_of(fid_mr, struct cxip_mr, mr_fid);

	cr_assert_eq(mr->md->handle_valid, hmem_dev_reg,
		     "Bad cxip_md handle_valid");
	cr_assert_eq(mr->md->info.iface, FI_HMEM_CUDA,
		     "Invalid CXIP MD iface: %d", mr->md->info.iface);

	ret = fi_close(&fid_mr->fid);
	cr_assert_eq(ret, FI_SUCCESS, "fi_close MR failed: %d", ret);

	cuda_ret = cudaFree(buf);
	cr_assert_eq(cuda_ret, cudaSuccess, "cudaFree  failed: %d", cuda_ret);

	cxit_teardown_msg();
}

/* Verify MD handle is false. */
Test(cuda, verify_noHmemDevReg)
{
	int ret;

	ret = setenv("FI_CXI_DISABLE_HMEM_DEV_REGISTER", "1", 1);
	cr_assert_eq(ret, 0, "setenv failed: %d", -errno);

	verify_dev_reg_handle(false);
}

/* Verify MD handle is true. */
Test(cuda, verify_hmemDevReg)
{
	int ret;

	ret = setenv("FI_CXI_DISABLE_HMEM_DEV_REGISTER", "0", 1);
	cr_assert_eq(ret, 0, "setenv failed: %d", -errno);

	verify_dev_reg_handle(true);
}
