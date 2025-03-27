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

#include <level_zero/ze_api.h>

#include "libcxi/libcxi.h"
#include "cxip.h"
#include "cxip_test_common.h"

static uint32_t ze_driver_count = 1;
static ze_driver_handle_t ze_driver;
static ze_context_handle_t ze_context;
static uint32_t ze_device_count = 1;
static ze_device_handle_t ze_device;
static ze_command_queue_handle_t ze_cq;
static const ze_device_mem_alloc_desc_t device_desc = {
	.stype		= ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC,
	.pNext		= NULL,
	.flags		= 0,
	.ordinal	= 0,
};
static const ze_host_mem_alloc_desc_t host_desc = {
	.stype		= ZE_STRUCTURE_TYPE_HOST_MEM_ALLOC_DESC,
	.pNext		= NULL,
	.flags		= 0,
};
static const ze_command_queue_desc_t cq_desc = {
	.stype		= ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC,
	.pNext		= NULL,
	.ordinal	= 0,
	.index		= 0,
	.flags		= 0,
	.mode		= ZE_COMMAND_QUEUE_MODE_SYNCHRONOUS,
	.priority	= ZE_COMMAND_QUEUE_PRIORITY_NORMAL,
};
static const ze_command_list_desc_t cl_desc = {
	.stype				= ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC,
	.pNext				= NULL,
	.commandQueueGroupOrdinal	= 0,
	.flags				= 0,
};

static void ze_init(void)
{
	ze_result_t ze_ret;
	ze_context_desc_t ze_context_desc = {};

	ze_ret = zeInit(ZE_INIT_FLAG_GPU_ONLY);
	cr_assert_eq(ze_ret, ZE_RESULT_SUCCESS, "zeInit failed: %d", ze_ret);

	ze_ret = zeDriverGet(&ze_driver_count, &ze_driver);
	cr_assert_eq(ze_ret, ZE_RESULT_SUCCESS, "zeDriverGet failed: %d",
		     ze_ret);

	ze_ret = zeContextCreate(ze_driver, &ze_context_desc, &ze_context);
	cr_assert_eq(ze_ret, ZE_RESULT_SUCCESS, "zeContextCreate failed: %d",
		     ze_ret);

	/* Only support a single device. */
	ze_ret = zeDeviceGet(ze_driver, &ze_device_count, &ze_device);
	cr_assert_eq(ze_ret, ZE_RESULT_SUCCESS, "zeDeviceGet failed: %d",
		     ze_ret);

	ze_ret = zeCommandQueueCreate(ze_context, ze_device, &cq_desc, &ze_cq);
	cr_assert_eq(ze_ret, ZE_RESULT_SUCCESS,
		     "zeCommandQueueCreate failed: %d", ze_ret);
}

static void ze_fini(void)
{
	ze_result_t ze_ret;

	ze_ret = zeCommandQueueDestroy(ze_cq);
	cr_assert_eq(ze_ret, ZE_RESULT_SUCCESS,
		     "zeCommandQueueDestroy failed: %d", ze_ret);

	ze_ret = zeContextDestroy(ze_context);
	cr_assert_eq(ze_ret, ZE_RESULT_SUCCESS,
		     "zeContextDestroy failed: %d", ze_ret);
}

static void ze_copy(void *dst, const void *src, size_t size)
{
	ze_command_list_handle_t cmd_list;
	ze_result_t ze_ret;

	ze_ret = zeCommandListCreate(ze_context, ze_device, &cl_desc,
				     &cmd_list);
	cr_assert_eq(ze_ret, ZE_RESULT_SUCCESS,
		     "zeCommandListCreate failed: %d", ze_ret);

	ze_ret = zeCommandListAppendMemoryCopy(cmd_list, dst, src, size, NULL,
					       0, NULL);
	cr_assert_eq(ze_ret, ZE_RESULT_SUCCESS,
		     "zeCommandListAppendMemoryCopy failed: %d", ze_ret);

	ze_ret = zeCommandListClose(cmd_list);
	cr_assert_eq(ze_ret, ZE_RESULT_SUCCESS,
		     "zeCommandListClose failed: %d", ze_ret);

	ze_ret = zeCommandQueueExecuteCommandLists(ze_cq, 1, &cmd_list, NULL);
	cr_assert_eq(ze_ret, ZE_RESULT_SUCCESS,
		     "zeCommandQueueExecuteCommandLists failed: %d", ze_ret);
}

TestSuite(ze, .timeout = CXIT_DEFAULT_TIMEOUT);

static void ze_message_runner(void *ze_send_buf, void *ze_recv_buf,
			      size_t buf_size)
{
	int ret;
	char *send_buf;
	char *recv_buf;
	struct fi_cq_tagged_entry cqe;
	int i;

	cxit_setup_msg();

	/* Send and recv buffer as used as bounce buffers for their ze
	 * counterparts. This is not true for zeMemAllocHost.
	 */
	send_buf = malloc(buf_size);
	cr_assert_neq(send_buf, NULL, "Failed to allocate memory");

	ret = open("/dev/urandom", O_RDONLY);
	cr_assert_neq(ret, -1, "open failed: %d", -errno);
	read(ret, send_buf, buf_size);
	close(ret);

	recv_buf = calloc(1, buf_size);
	cr_assert_neq(send_buf, NULL, "Failed to allocate memory");

	ze_copy(ze_send_buf, send_buf, buf_size);

	ret = fi_recv(cxit_ep, ze_recv_buf, buf_size, NULL, cxit_ep_fi_addr,
		      NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed: %d", ret);

	ret = fi_send(cxit_ep, ze_send_buf, buf_size, NULL, cxit_ep_fi_addr,
		      NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_send failed: %d", ret);

	do {
		ret = fi_cq_read(cxit_rx_cq, &cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	do {
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	ze_copy(recv_buf, ze_recv_buf, buf_size);

	for (i = 0; i < buf_size; i++)
		cr_assert_eq(send_buf[i], recv_buf[i],
			     "Data corruption at byte %d", i);

	free(recv_buf);
	free(send_buf);

	cxit_teardown_msg();
}

Test(ze, messaging_devMemory)
{
	ze_result_t ze_ret;
	void *ze_send_buf;
	void *ze_recv_buf;
	size_t buf_size = 1048576;

	ze_init();

	/* Ze buffers will be used for RDMA. */
	ze_ret = zeMemAllocDevice(ze_context, &device_desc, buf_size, 0,
				  ze_device, &ze_send_buf);
	cr_assert_eq(ze_ret, ZE_RESULT_SUCCESS, "zeMemAllocDevice failed: %d",
		     ze_ret);

	ze_ret = zeMemAllocDevice(ze_context, &device_desc, buf_size, 0,
				  ze_device, &ze_recv_buf);
	cr_assert_eq(ze_ret, ZE_RESULT_SUCCESS, "zeMemAllocDevice failed: %d",
		     ze_ret);

	ze_message_runner(ze_send_buf, ze_recv_buf, buf_size);

	ze_ret = zeMemFree(ze_context, ze_recv_buf);
	cr_assert_eq(ze_ret, ZE_RESULT_SUCCESS, "zeMemFree failed: %d",
		     ze_ret);

	ze_ret = zeMemFree(ze_context, ze_send_buf);
	cr_assert_eq(ze_ret, ZE_RESULT_SUCCESS, "zeMemFree failed: %d",
		     ze_ret);

	ze_fini();
}

Test(ze, messaging_hostMemory)
{
	ze_result_t ze_ret;
	void *ze_send_buf;
	void *ze_recv_buf;
	size_t buf_size = 1048576;

	ze_init();

	/* Ze buffers will be used for RDMA. */
	ze_ret = zeMemAllocHost(ze_context, &host_desc, buf_size, 0,
				&ze_send_buf);
	cr_assert_eq(ze_ret, ZE_RESULT_SUCCESS, "zeMemAllocDevice failed: %d",
		     ze_ret);

	ze_ret = zeMemAllocHost(ze_context, &host_desc, buf_size, 0,
				&ze_recv_buf);
	cr_assert_eq(ze_ret, ZE_RESULT_SUCCESS, "zeMemAllocDevice failed: %d",
		     ze_ret);

	ze_message_runner(ze_send_buf, ze_recv_buf, buf_size);

	ze_ret = zeMemFree(ze_context, ze_recv_buf);
	cr_assert_eq(ze_ret, ZE_RESULT_SUCCESS, "zeMemFree failed: %d",
		     ze_ret);

	ze_ret = zeMemFree(ze_context, ze_send_buf);
	cr_assert_eq(ze_ret, ZE_RESULT_SUCCESS, "zeMemFree failed: %d",
		     ze_ret);

	ze_fini();
}
