/*
 * Copyright (c) 2023 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <ctype.h>

#include <criterion/criterion.h>
#include <criterion/parameterized.h>
#include <pthread.h>
#include <hsa/hsa.h>

#include "libcxi/libcxi.h"
#include "cxip.h"
#include "cxip_test_common.h"

#define MAX_MSG_SIZE 1048576U
#define MAX_BUF_OFFSET 65536U
#define REGION_MAX 255

static unsigned int seed;
static hsa_agent_t agent;
static hsa_region_t regions[REGION_MAX];
static int num_regions;
static hsa_region_t coarse_grain;
bool coarse_grain_valid;
static hsa_region_t fine_grain;
bool fine_grain_valid;

static hsa_status_t get_gpu_agent(hsa_agent_t agent, void *data) {
	hsa_status_t status;
	hsa_device_type_t device_type;

	status = hsa_agent_get_info(agent, HSA_AGENT_INFO_DEVICE, &device_type);
	if (HSA_STATUS_SUCCESS == status && HSA_DEVICE_TYPE_GPU == device_type) {
		hsa_agent_t* ret = (hsa_agent_t*)data;
		*ret = agent;
		return HSA_STATUS_INFO_BREAK;
	}

	return HSA_STATUS_SUCCESS;
}

static hsa_status_t callback_get_num_regions(hsa_region_t region, void* data) {
	int *num_regions = (int *)data;
	(*num_regions)++;
	return HSA_STATUS_SUCCESS;
}

static hsa_status_t callback_get_regions(hsa_region_t region, void* data) {
	hsa_region_t **region_list = (hsa_region_t **)data;
	**region_list = region;
	(*region_list)++;
	return HSA_STATUS_SUCCESS;
}

static void hsa_test_init(void)
{
	hsa_status_t hsa_ret;
	hsa_region_t *ptr_reg = regions;
	int i;
	size_t size_r;

	enable_cxi_hmem_ops = 0;
	seed = time(NULL);
	srand(seed);

	hsa_ret = hsa_init();
	cr_assert_eq(hsa_ret, HSA_STATUS_SUCCESS);

	hsa_ret = hsa_iterate_agents(get_gpu_agent, &agent);
	cr_assert_eq(hsa_ret, HSA_STATUS_INFO_BREAK);

	hsa_ret = hsa_agent_iterate_regions(agent, callback_get_num_regions,
					    &num_regions);
	cr_assert_eq(hsa_ret, HSA_STATUS_SUCCESS);
	cr_assert(num_regions <= REGION_MAX);

	hsa_ret = hsa_agent_iterate_regions(agent, callback_get_regions,
					    &ptr_reg);
	cr_assert_eq(hsa_ret, HSA_STATUS_SUCCESS);

	for (i = 0; i < num_regions; i++) {
		hsa_ret = hsa_region_get_info(regions[i],
					      HSA_REGION_INFO_GLOBAL_FLAGS,
					      &size_r);
		cr_assert_eq(hsa_ret, HSA_STATUS_SUCCESS);

		if (size_r & HSA_REGION_GLOBAL_FLAG_FINE_GRAINED &&
		    !fine_grain_valid) {
			fine_grain = regions[i];
			fine_grain_valid = true;
		}

		if (size_r & HSA_REGION_GLOBAL_FLAG_COARSE_GRAINED &&
		    !coarse_grain_valid) {
			coarse_grain = regions[i];
			coarse_grain_valid = true;
		}

		if (fine_grain_valid && coarse_grain_valid)
			break;
	}

	cr_assert_eq(coarse_grain_valid, true,
		     "Failed to find coarse grain memory");
	cr_assert_eq(fine_grain_valid, true,
		     "Failed to find fine grain memory");
}

static void hsa_test_fini(void)
{
	hsa_status_t hsa_ret;

	hsa_ret = hsa_shut_down();
	cr_assert_eq(hsa_ret, HSA_STATUS_SUCCESS);
}

TestSuite(hsa, .timeout = CXIT_DEFAULT_TIMEOUT, .init = hsa_test_init,
	  .fini = hsa_test_fini);

static void hsa_message_runner(void *hsa_send_buf, void *hsa_recv_buf,
			       size_t buf_size, bool device_only_mem,
			       bool unexpected)
{
	int ret;
	char *send_buf;
	char *recv_buf;
	struct fi_cq_tagged_entry cqe;
	int i;
	hsa_status_t hsa_ret;
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
		send_buf = hsa_send_buf;
		recv_buf = hsa_recv_buf;
	}

	for (j = 0; j < 2; j++) {

		ret = open("/dev/urandom", O_RDONLY);
		cr_assert_neq(ret, -1, "open failed: %d", -errno);
		read(ret, send_buf, buf_size);
		close(ret);

		if (device_only_mem) {
			hsa_ret = hsa_memory_copy(hsa_send_buf, send_buf,
						  buf_size);
			cr_assert_eq(hsa_ret, HSA_STATUS_SUCCESS,
				     "hsaMemcpy failed: %d", hsa_ret);
		}

		if (unexpected) {
			ret = fi_send(cxit_ep, hsa_send_buf, buf_size, NULL, cxit_ep_fi_addr,
				      NULL);
			cr_assert_eq(ret, FI_SUCCESS, "fi_send failed: %d", ret);

			ret = fi_recv(cxit_ep, hsa_recv_buf, buf_size, NULL, cxit_ep_fi_addr,
				      NULL);
			cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed: %d", ret);
		} else {
			ret = fi_recv(cxit_ep, hsa_recv_buf, buf_size, NULL, cxit_ep_fi_addr,
				      NULL);
			cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed: %d", ret);

			ret = fi_send(cxit_ep, hsa_send_buf, buf_size, NULL, cxit_ep_fi_addr,
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
			hsa_ret = hsa_memory_copy(recv_buf, hsa_recv_buf,
						  buf_size);
			cr_assert_eq(hsa_ret, HSA_STATUS_SUCCESS,
				     "hsaMemcpy failed: %d", hsa_ret);
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

enum mem_type {
	COARSE,
	FINE,
};

static void hsa_dev_memory_test(size_t buf_size, size_t buf_offset,
				 bool unexpected, bool hmem_dev_reg,
				 enum mem_type type)
{
	hsa_status_t hsa_ret;
	void *hsa_send_buf;
	void *hsa_recv_buf;
	int ret;
	hsa_region_t region;

	if (hmem_dev_reg)
		ret = setenv("FI_CXI_DISABLE_HMEM_DEV_REGISTER", "0", 1);
	else
		ret = setenv("FI_CXI_DISABLE_HMEM_DEV_REGISTER", "1", 1);
	cr_assert_eq(ret, 0, "setenv failed: %d", -errno);

	if (type == COARSE)
		region = coarse_grain;
	else
		region = fine_grain;

	/* hsa buffers will be used for RDMA. */
	hsa_ret = hsa_memory_allocate(region, buf_size + buf_offset,
				      &hsa_send_buf);
	cr_assert_eq(hsa_ret, HSA_STATUS_SUCCESS, "hsaMalloc failed: %d", hsa_ret);

	hsa_ret = hsa_memory_allocate(region, buf_size + buf_offset,
				      &hsa_recv_buf);
	cr_assert_eq(hsa_ret, HSA_STATUS_SUCCESS, "hsaMalloc failed: %d", hsa_ret);

	hsa_message_runner((void *)((char *)hsa_send_buf + buf_offset),
			   (void *)((char *)hsa_recv_buf + buf_offset),
			   buf_size, true, unexpected);

	hsa_ret = hsa_memory_free(hsa_recv_buf);
	cr_assert_eq(hsa_ret, HSA_STATUS_SUCCESS, "hsaFree  failed: %d",
		     hsa_ret);

	hsa_ret = hsa_memory_free(hsa_send_buf);
	cr_assert_eq(hsa_ret, HSA_STATUS_SUCCESS, "hsaFree  failed: %d",
		     hsa_ret);

}

/* Test messaging using rendezvous, device memory, and HMEM device memory
 * registration for load/store access.
 */
Test(hsa, messaging_devMemory_rdvz_hmemDevReg_coarse)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % MAX_MSG_SIZE;
		if (buf_size > 65536)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, false, true, COARSE);
}

/* Test messaging using eager, device memory, and HMEM device memory
 * registration for load/store access.
 */
Test(hsa, messaging_devMemory_eager_hmemDevReg_coarse)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % 1024;
		if (buf_size > 256)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, false, true, COARSE);
}

/* Test messaging using IDC, device memory, and HMEM device memory
 * registration for load/store access.
 */
Test(hsa, messaging_devMemory_idc_hmemDevReg_coarse)
{
	size_t buf_size;
	size_t buf_offset;

	buf_size = rand() % 128;
	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, false, true, COARSE);
}

/* Test messaging using rendezvous, device memory, unexpected messaging, and
 * HMEM device memory registration for load/store access.
 */
Test(hsa, messaging_devMemory_rdvz_unexpected_hmemDevReg_coarse)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % MAX_MSG_SIZE;
		if (buf_size > 65536)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, true, true, COARSE);
}

/* Test messaging using eager, device memory, unexpected messaging, and
 * HMEM device memory registration for load/store access.
 */
Test(hsa, messaging_devMemory_eager_unexpected_hmemDevReg_coarse)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % 1024;
		if (buf_size > 256)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, true, true, COARSE);
}

/* Test messaging using IDC, device memory, unexpected messaging, and
 * HMEM device memory registration for load/store access.
 */
Test(hsa, messaging_devMemory_idc_unexpected_hmemDevReg_coarse)
{
	size_t buf_size;
	size_t buf_offset;

	buf_size = rand() % 128;
	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, true, true, COARSE);
}

/* Test messaging using rendezvous, device memory, and without HMEM device memory
 * registration for load/store access.
 */
Test(hsa, messaging_devMemory_rdvz_noHmemDevReg_coarse)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % MAX_MSG_SIZE;
		if (buf_size > 65536)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, false, false, COARSE);
}

/* Test messaging using eager, device memory, and without HMEM device memory
 * registration for load/store access.
 */
Test(hsa, messaging_devMemory_eager_noHmemDevReg_coarse)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % 1024;
		if (buf_size > 256)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, false, false, COARSE);
}

/* Test messaging using IDC, device memory, and without HMEM device memory
 * registration for load/store access.
 */
Test(hsa, messaging_devMemory_idc_noHmemDevReg_coarse)
{
	size_t buf_size;
	size_t buf_offset;

	buf_size = rand() % 128;
	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, false, false, COARSE);
}

/* Test messaging using rendezvous, device memory, unexpected messaging, and
 * without HMEM device memory registration for load/store access.
 */
Test(hsa, messaging_devMemory_rdvz_unexpected_noHmemDevReg_coarse)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % MAX_MSG_SIZE;
		if (buf_size > 65536)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, true, false, COARSE);
}

/* Test messaging using eager, device memory, unexpected messaging, and
 * without HMEM device memory registration for load/store access.
 */
Test(hsa, messaging_devMemory_eager_unexpected_noHmemDevReg_coarse)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % 1024;
		if (buf_size > 256)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, true, false, COARSE);
}

/* Test messaging using IDC, device memory, unexpected messaging, and
 * without HMEM device memory registration for load/store access.
 */
Test(hsa, messaging_devMemory_idc_unexpected_noHmemDevReg_coarse)
{
	size_t buf_size;
	size_t buf_offset;

	buf_size = rand() % 128;
	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, true, false, COARSE);
}

/* Test messaging using rendezvous, device memory, and HMEM device memory
 * registration for load/store access.
 */
Test(hsa, messaging_devMemory_rdvz_hmemDevReg_fine)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % MAX_MSG_SIZE;
		if (buf_size > 65536)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, false, true, FINE);
}

/* Test messaging using eager, device memory, and HMEM device memory
 * registration for load/store access.
 */
Test(hsa, messaging_devMemory_eager_hmemDevReg_fine)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % 1024;
		if (buf_size > 256)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, false, true, FINE);
}

/* Test messaging using IDC, device memory, and HMEM device memory
 * registration for load/store access.
 */
Test(hsa, messaging_devMemory_idc_hmemDevReg_fine)
{
	size_t buf_size;
	size_t buf_offset;

	buf_size = rand() % 128;
	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, false, true, FINE);
}

/* Test messaging using rendezvous, device memory, unexpected messaging, and
 * HMEM device memory registration for load/store access.
 */
Test(hsa, messaging_devMemory_rdvz_unexpected_hmemDevReg_fine)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % MAX_MSG_SIZE;
		if (buf_size > 65536)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, true, true, FINE);
}

/* Test messaging using eager, device memory, unexpected messaging, and
 * HMEM device memory registration for load/store access.
 */
Test(hsa, messaging_devMemory_eager_unexpected_hmemDevReg_fine)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % 1024;
		if (buf_size > 256)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, true, true, FINE);
}

/* Test messaging using IDC, device memory, unexpected messaging, and
 * HMEM device memory registration for load/store access.
 */
Test(hsa, messaging_devMemory_idc_unexpected_hmemDevReg_fine)
{
	size_t buf_size;
	size_t buf_offset;

	buf_size = rand() % 128;
	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, true, true, FINE);
}

/* Test messaging using rendezvous, device memory, and without HMEM device memory
 * registration for load/store access.
 */
Test(hsa, messaging_devMemory_rdvz_noHmemDevReg_fine)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % MAX_MSG_SIZE;
		if (buf_size > 65536)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, false, false, FINE);
}

/* Test messaging using eager, device memory, and without HMEM device memory
 * registration for load/store access.
 */
Test(hsa, messaging_devMemory_eager_noHmemDevReg_fine)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % 1024;
		if (buf_size > 256)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, false, false, FINE);
}

/* Test messaging using IDC, device memory, and without HMEM device memory
 * registration for load/store access.
 */
Test(hsa, messaging_devMemory_idc_noHmemDevReg_fine)
{
	size_t buf_size;
	size_t buf_offset;

	buf_size = rand() % 128;
	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, false, false, FINE);
}

/* Test messaging using rendezvous, device memory, unexpected messaging, and
 * without HMEM device memory registration for load/store access.
 */
Test(hsa, messaging_devMemory_rdvz_unexpected_noHmemDevReg_fine)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % MAX_MSG_SIZE;
		if (buf_size > 65536)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, true, false, FINE);
}

/* Test messaging using eager, device memory, unexpected messaging, and
 * without HMEM device memory registration for load/store access.
 */
Test(hsa, messaging_devMemory_eager_unexpected_noHmemDevReg_fine)
{
	size_t buf_size;
	size_t buf_offset;

	while (true) {
		buf_size = rand() % 1024;
		if (buf_size > 256)
			break;
	}

	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, true, false, FINE);
}

/* Test messaging using IDC, device memory, unexpected messaging, and
 * without HMEM device memory registration for load/store access.
 */
Test(hsa, messaging_devMemory_idc_unexpected_noHmemDevReg_fine)
{
	size_t buf_size;
	size_t buf_offset;

	buf_size = rand() % 128;
	buf_offset = rand() % MAX_BUF_OFFSET;

	hsa_dev_memory_test(buf_size, buf_offset, true, false, FINE);
}

static void verify_dev_reg_handle(bool hmem_dev_reg, enum mem_type type)
{
	int ret;
	void *buf;
	hsa_status_t hsa_ret;
	struct fid_mr *fid_mr;
	size_t buf_size = 1024;
	struct cxip_mr *mr;
	hsa_region_t region;

	cxit_setup_msg();

	if (type == COARSE)
		region = coarse_grain;
	else
		region = fine_grain;

	hsa_ret = hsa_memory_allocate(region, buf_size, &buf);
	cr_assert_eq(hsa_ret, HSA_STATUS_SUCCESS, "hsaMalloc failed: %d",
		     hsa_ret);

	ret = fi_mr_reg(cxit_domain, buf, buf_size, FI_READ, 0, 0x123, 0,
			&fid_mr, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_reg failed: %d", ret);

	mr = container_of(fid_mr, struct cxip_mr, mr_fid);

	cr_assert_eq(mr->md->handle_valid, hmem_dev_reg,
		     "Bad cxip_md handle_valid");
	cr_assert_eq(mr->md->info.iface, FI_HMEM_ROCR,
		     "Invalid CXIP MD iface: %d", mr->md->info.iface);

	ret = fi_close(&fid_mr->fid);
	cr_assert_eq(ret, FI_SUCCESS, "fi_close MR failed: %d", ret);

	hsa_ret = hsa_memory_free(buf);
	cr_assert_eq(hsa_ret, HSA_STATUS_SUCCESS, "hsaFree  failed: %d",
		     hsa_ret);

	cxit_teardown_msg();
}

/* Verify MD handle is false. */
Test(hsa, verify_noHmemDevReg_coarse)
{
	int ret;

	ret = setenv("FI_CXI_DISABLE_HMEM_DEV_REGISTER", "1", 1);
	cr_assert_eq(ret, 0, "setenv failed: %d", -errno);

	verify_dev_reg_handle(false, COARSE);
}

/* Verify MD handle is true. */
Test(hsa, verify_hmemDevReg_coarse)
{
	int ret;

	ret = setenv("FI_CXI_DISABLE_HMEM_DEV_REGISTER", "0", 1);
	cr_assert_eq(ret, 0, "setenv failed: %d", -errno);

	verify_dev_reg_handle(true, COARSE);
}

/* Verify MD handle is false. */
Test(hsa, verify_noHmemDevReg_fine)
{
	int ret;

	ret = setenv("FI_CXI_DISABLE_HMEM_DEV_REGISTER", "1", 1);
	cr_assert_eq(ret, 0, "setenv failed: %d", -errno);

	verify_dev_reg_handle(false, FINE);
}

/* Verify MD handle is true. */
Test(hsa, verify_hmemDevReg_fine)
{
	int ret;

	ret = setenv("FI_CXI_DISABLE_HMEM_DEV_REGISTER", "0", 1);
	cr_assert_eq(ret, 0, "setenv failed: %d", -errno);

	verify_dev_reg_handle(true, FINE);
}
