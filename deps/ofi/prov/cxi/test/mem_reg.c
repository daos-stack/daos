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

#include "libcxi/libcxi.h"
#include "cxip.h"
#include "cxip_test_common.h"

TestSuite(memReg, .timeout = CXIT_DEFAULT_TIMEOUT);

static void hmem_dev_reg_test_runner(bool dev_reg, bool cache_enable)
{
	int ret;
	void *buf;
	size_t buf_size = 1234;
	struct fid_mr *mr;
	struct cxip_mr *cxi_mr;

	if (dev_reg)
		ret = setenv("FI_CXI_DISABLE_HMEM_DEV_REGISTER", "0", 1);
	else
		ret = setenv("FI_CXI_DISABLE_HMEM_DEV_REGISTER", "1", 1);
	cr_assert_eq(ret, 0,
		     "Failed to set FI_CXI_DISABLE_HMEM_DEV_REGISTER %d",
		     -errno);

	if (cache_enable)
		ret = setenv("FI_MR_CACHE_MONITOR", "memhooks", 1);
	else
		ret = setenv("FI_MR_CACHE_MONITOR", "disabled", 1);
	cr_assert_eq(ret, 0,
		     "Failed to set FI_MR_CACHE_MONITOR %d",
		     -errno);

	buf = malloc(buf_size);
	cr_assert_neq(buf, NULL, "Failed to alloc mem");

	cxit_setup_msg();

	ret = fi_mr_reg(cxit_domain, buf, buf_size, FI_READ | FI_WRITE, 0, 0, 0,
			&mr, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_reg failed: %d", ret);

	ret = fi_mr_bind(mr, &cxit_ep->fid, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_bind failed: %d", ret);

	ret = fi_mr_enable(mr);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_enable failed: %d", ret);

	/* Have to examine the struct to determine if correct behavior is
	 * happening.
	 */
	cxi_mr = container_of(mr, struct cxip_mr, mr_fid);
	if (dev_reg)
		cr_assert_eq(cxi_mr->md->handle_valid, true,
			      "Bad cxip_md handle_valid");
	else
		cr_assert_eq(cxi_mr->md->handle_valid, false,
			     "Bad cxip_md host_addr");
	cr_assert_eq(cxi_mr->md->cached, cache_enable, "Bad cxip_md cached");

	ret = fi_close(&mr->fid);
	cr_assert_eq(ret, FI_SUCCESS, "fi_close failed: %d", ret);

	cxit_teardown_msg();
	free(buf);
}

Test(memReg, disableHmemDevRegisterEnabled_mrCacheEnabled)
{
	hmem_dev_reg_test_runner(true, true);
}

Test(memReg, disableHmemDevRegisterEnabled_mrCacheDisabled)
{
	hmem_dev_reg_test_runner(true, false);
}

Test(memReg, disableHmemDevRegisterDisabled_mrCacheEnabled)
{
	hmem_dev_reg_test_runner(false, true);
}

Test(memReg, disableHmemDevRegisterDisabled_mrCacheDisabled)
{
	hmem_dev_reg_test_runner(false, false);
}

static void system_mem_dev_reg_test_runner(bool system_mem_cache_enabled,
					   bool hmem_dev_reg_enabled)
{
	char *send_buf;
	char *recv_buf;
	size_t buf_size = 1234;
	int ret;
	struct fi_cq_tagged_entry cqe;
	int i;

	if (system_mem_cache_enabled)
		ret = setenv("FI_MR_CACHE_MONITOR", "memhooks", 1);
	else
		ret = setenv("FI_MR_CACHE_MONITOR", "disabled", 1);
	cr_assert_eq(ret, 0,
		     "Failed to set FI_MR_CACHE_MONITOR %d",
		     -errno);

	if (hmem_dev_reg_enabled)
		ret = setenv("FI_CXI_DISABLE_HMEM_DEV_REGISTER", "0", 1);
	else
		ret = setenv("FI_CXI_DISABLE_HMEM_DEV_REGISTER", "1", 1);
	cr_assert_eq(ret, 0,
		     "Failed to set FI_CXI_DISABLE_HMEM_DEV_REGISTER %d",
		     -errno);

	send_buf = calloc(1, buf_size);
	cr_assert_neq(send_buf, NULL, "Failed to alloc mem");

	recv_buf = calloc(1, buf_size);
	cr_assert_neq(recv_buf, NULL, "Failed to alloc mem");

	ret = open("/dev/urandom", O_RDONLY);
	cr_assert_neq(ret, -1, "open failed: %d", -errno);
	read(ret, send_buf + 1, buf_size - 1);
	close(ret);

	cxit_setup_msg();

	ret = fi_recv(cxit_ep, recv_buf + 1, buf_size - 1, NULL,
		      cxit_ep_fi_addr, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed: %d", ret);

	ret = fi_send(cxit_ep, send_buf + 1, buf_size - 1, NULL,
		      cxit_ep_fi_addr, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_send failed: %d", ret);

	do {
		ret = fi_cq_read(cxit_rx_cq, &cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	do {
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	for (i = 0; i < buf_size; i++)
		cr_assert_eq(send_buf[i], recv_buf[i],
			     "Data corruption at byte %d", i);

	cxit_teardown_msg();

	free(send_buf);
	free(recv_buf);
}

Test(memReg, systemMemNoCache_enableHmemDevRegister)
{
	system_mem_dev_reg_test_runner(false, true);
}

Test(memReg, systemMemCache_enableHmemDevRegister)
{
	system_mem_dev_reg_test_runner(true, true);
}

Test(memReg, systemMemNoCache_disableHmemDevRegister)
{
	system_mem_dev_reg_test_runner(false, false);
}

Test(memReg, systemMemCache_disableHmemDevRegister)
{
	system_mem_dev_reg_test_runner(true, false);
}
