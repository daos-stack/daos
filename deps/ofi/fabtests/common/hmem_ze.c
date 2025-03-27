/*
 * Copyright (c) 2020 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include "hmem.h"
#include "shared.h"

#if HAVE_ZE

#include <dlfcn.h>
#include <level_zero/ze_api.h>

#define ZE_MAX_DEVICES 32

static ze_context_handle_t context;
static ze_device_handle_t devices[ZE_MAX_DEVICES];
static ze_command_queue_handle_t cmd_queue;
static ze_command_list_handle_t cmd_list;

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

static void *libze_handle;
struct libze_ops libze_ops;
int init_libze_ops(void)
{
	libze_handle = dlopen("libze_loader.so.1", RTLD_NOW);
	if (!libze_handle) {
		FT_ERR("Failed to dlopen libze_loader.so\n");
		goto err_out;
	}

	libze_ops.zeInit = dlsym(libze_handle, "zeInit");
	if (!libze_ops.zeInit) {
		FT_ERR("Failed to find zeInit\n");
		goto err_dlclose;
	}

	libze_ops.zeDriverGet = dlsym(libze_handle, "zeDriverGet");
	if (!libze_ops.zeDriverGet) {
		FT_ERR("Failed to find zeDriverGet\n");
		goto err_dlclose;
	}

	libze_ops.zeDriverGetExtensionFunctionAddress = dlsym(libze_handle,
					"zeDriverGetExtensionFunctionAddress");
	if (!libze_ops.zeDriverGetExtensionFunctionAddress) {
		FT_ERR("Failed to find zeDriverGetExtensionFunctionAddress\n");
		goto err_dlclose;
	}

	libze_ops.zeDeviceGet = dlsym(libze_handle, "zeDeviceGet");
	if (!libze_ops.zeDeviceGet) {
		FT_ERR("Failed to find zeDeviceGet\n");
		goto err_dlclose;
	}

	libze_ops.zeDeviceGetProperties = dlsym(libze_handle,
						"zeDeviceGetProperties");
	if (!libze_ops.zeDeviceGetProperties) {
		FT_ERR("Failed to find zeDeviceGetProperties\n");
		goto err_dlclose;
	}

	libze_ops.zeDeviceGetSubDevices = dlsym(libze_handle,
						"zeDeviceGetSubDevices");
	if (!libze_ops.zeDeviceGetSubDevices) {
		FT_ERR("Failed to find zeDeviceGetSubDevices\n");
		goto err_dlclose;
	}

	libze_ops.zeDeviceGetCommandQueueGroupProperties = dlsym(libze_handle,
									"zeDeviceGetCommandQueueGroupProperties");
	if (!libze_ops.zeDeviceGetCommandQueueGroupProperties) {
		FT_ERR("Failed to find zeDeviceGetCommandQueueGroupProperties\n");
		goto err_dlclose;
	}

	libze_ops.zeDeviceCanAccessPeer = dlsym(libze_handle, "zeDeviceCanAccessPeer");
	if (!libze_ops.zeDeviceCanAccessPeer) {
		FT_ERR("Failed to find zeDeviceCanAccessPeer\n");
		goto err_dlclose;
	}

	libze_ops.zeContextCreate = dlsym(libze_handle, "zeContextCreate");
	if (!libze_ops.zeContextCreate) {
		FT_ERR("Failed to find zeContextCreate\n");
		goto err_dlclose;
	}

	libze_ops.zeContextDestroy = dlsym(libze_handle, "zeContextDestroy");
	if (!libze_ops.zeContextDestroy) {
		FT_ERR("Failed to find zeContextDestroy\n");
		goto err_dlclose;
	}

	libze_ops.zeContextDestroy = dlsym(libze_handle, "zeContextDestroy");
	if (!libze_ops.zeContextDestroy) {
		FT_ERR("Failed to find zeContextDestroy\n");
		goto err_dlclose;
	}

	libze_ops.zeCommandQueueCreate = dlsym(libze_handle, "zeCommandQueueCreate");
	if (!libze_ops.zeCommandQueueCreate) {
		FT_ERR("Failed to find zeCommandQueueCreate\n");
		goto err_dlclose;
	}

	libze_ops.zeCommandQueueDestroy = dlsym(libze_handle, "zeCommandQueueDestroy");
	if (!libze_ops.zeCommandQueueDestroy) {
		FT_ERR("Failed to find zeCommandQueueDestroy\n");
		goto err_dlclose;
	}

	libze_ops.zeCommandQueueExecuteCommandLists = dlsym(libze_handle, "zeCommandQueueExecuteCommandLists");
	if (!libze_ops.zeCommandQueueExecuteCommandLists) {
		FT_ERR("Failed to find zeCommandQueueExecuteCommandLists\n");
		goto err_dlclose;
	}

	libze_ops.zeCommandQueueSynchronize = dlsym(libze_handle,
						"zeCommandQueueSynchronize");
	if (!libze_ops.zeCommandQueueSynchronize) {
		FT_ERR("Failed to find zeCommandQueueSynchronize\n");
		goto err_dlclose;
	}

	libze_ops.zeCommandListCreate = dlsym(libze_handle, "zeCommandListCreate");
	if (!libze_ops.zeCommandListCreate) {
		FT_ERR("Failed to find zeCommandListCreate\n");
		goto err_dlclose;
	}

	libze_ops.zeCommandListCreateImmediate = dlsym(libze_handle,
						"zeCommandListCreateImmediate");
	if (!libze_ops.zeCommandListCreateImmediate) {
		FT_ERR("Failed to find zeCommandListCreateImmediate\n");
		goto err_dlclose;
	}

	libze_ops.zeCommandListDestroy = dlsym(libze_handle, "zeCommandListDestroy");
	if (!libze_ops.zeCommandListDestroy) {
		FT_ERR("Failed to find zeCommandListDestroy\n");
		goto err_dlclose;
	}

	libze_ops.zeCommandListReset = dlsym(libze_handle,
					     "zeCommandListReset");
	if (!libze_ops.zeCommandListReset) {
		FT_ERR("Failed to find zeCommandListReset\n");
		goto err_dlclose;
	}

	libze_ops.zeCommandListClose = dlsym(libze_handle,
					     "zeCommandListClose");
	if (!libze_ops.zeCommandListClose) {
		FT_ERR("Failed to find zeCommandListClose\n");
		goto err_dlclose;
	}

	libze_ops.zeCommandListAppendMemoryCopy = dlsym(libze_handle, "zeCommandListAppendMemoryCopy");
	if (!libze_ops.zeCommandListAppendMemoryCopy) {
		FT_ERR("Failed to find zeCommandListAppendMemoryCopy\n");
		goto err_dlclose;
	}

	libze_ops.zeCommandListAppendMemoryFill = dlsym(libze_handle, "zeCommandListAppendMemoryFill");
	if (!libze_ops.zeCommandListAppendMemoryFill) {
		FT_ERR("Failed to find zeCommandListAppendMemoryFill\n");
		goto err_dlclose;
	}

	libze_ops.zeMemAllocHost = dlsym(libze_handle, "zeMemAllocHost");
	if (!libze_ops.zeMemAllocHost) {
		FT_ERR("Failed to find zeMemAllocHost\n");
		goto err_dlclose;
	}

	libze_ops.zeMemAllocDevice = dlsym(libze_handle, "zeMemAllocDevice");
	if (!libze_ops.zeMemAllocDevice) {
		FT_ERR("Failed to find zeMemAllocDevice\n");
		goto err_dlclose;
	}

	libze_ops.zeMemAllocShared = dlsym(libze_handle, "zeMemAllocShared");
	if (!libze_ops.zeMemAllocShared) {
		FT_ERR("Failed to find zeMemAllocShared\n");
		goto err_dlclose;
	}

	libze_ops.zeMemGetAllocProperties = dlsym(libze_handle,
						  "zeMemGetAllocProperties");
	if (!libze_ops.zeMemGetAllocProperties) {
		FT_ERR("Failed to find zeMemGetAllocProperties\n");
		goto err_dlclose;
	}

	libze_ops.zeMemGetAddressRange = dlsym(libze_handle,
					       "zeMemGetAddressRange");
	if (!libze_ops.zeMemGetAddressRange) {
		FT_ERR("Failed to find zeMemGetAddressRange\n");
		goto err_dlclose;
	}

	libze_ops.zeMemGetIpcHandle = dlsym(libze_handle, "zeMemGetIpcHandle");
	if (!libze_ops.zeMemGetIpcHandle) {
		FT_ERR("Failed to find zeMemGetIpcHandle\n");
		goto err_dlclose;
	}

	libze_ops.zeMemFree = dlsym(libze_handle, "zeMemFree");
	if (!libze_ops.zeMemFree) {
		FT_ERR("Failed to find zeMemFree\n");
		goto err_dlclose;
	}
	return FI_SUCCESS;

err_dlclose:
	dlclose(libze_handle);

err_out:
	return -FI_ENODATA;
}

static void cleanup_libze_ops(void)
{
	dlclose(libze_handle);
}

int ft_ze_init(void)
{
	ze_driver_handle_t driver;
	ze_context_desc_t context_desc = {0};
	ze_result_t ze_ret;
	uint32_t count;

	if (init_libze_ops())
		return -FI_EIO;

	ze_ret = (*libze_ops.zeInit)(ZE_INIT_FLAG_GPU_ONLY);
	if (ze_ret)
		return -FI_EIO;

	count = 1;
	ze_ret = (*libze_ops.zeDriverGet)(&count, &driver);
	if (ze_ret)
		return -FI_EIO;

	ze_ret = (*libze_ops.zeContextCreate)(driver, &context_desc, &context);
	if (ze_ret)
		return -FI_EIO;

	count = 0;
	ze_ret = (*libze_ops.zeDeviceGet)(driver, &count, NULL);
	if (ze_ret || count > ZE_MAX_DEVICES)
		goto err;;

	ze_ret = (*libze_ops.zeDeviceGet)(driver, &count, devices);
	if (ze_ret)
		goto err;

	ze_ret = (*libze_ops.zeCommandQueueCreate)( context,
				devices[opts.device], &cq_desc, &cmd_queue);
	if (ze_ret)
		goto err;

	ze_ret = (*libze_ops.zeCommandListCreate)(context,
				devices[opts.device], &cl_desc, &cmd_list);
	if (ze_ret)
		goto err;

	return FI_SUCCESS;

err:
	(void) ft_ze_cleanup();
	return -FI_EIO;
}

int ft_ze_cleanup(void)
{
	int ret = FI_SUCCESS;

	if (cmd_list) {
		ret = (*libze_ops.zeCommandListDestroy)(cmd_list);
		if (ret)
			return -FI_EINVAL;
	}

	if (cmd_queue) {
		ret = (*libze_ops.zeCommandQueueDestroy)(cmd_queue);
		if (ret)
			return -FI_EINVAL;
	}

	if ((*libze_ops.zeContextDestroy)(context))
		return -FI_EINVAL;

	cleanup_libze_ops();
	return ret;
}

int ft_ze_alloc(uint64_t device, void **buf, size_t size)
{
	return (*libze_ops.zeMemAllocDevice)(context, &device_desc, size, 16,
					     devices[device], buf) ?
			-FI_EINVAL : 0;
}

int ft_ze_alloc_host(void **buffer, size_t size)
{
	return (*libze_ops.zeMemAllocHost)(context, &host_desc, size, 16,
					   buffer) ? -FI_EINVAL : FI_SUCCESS;
}

int ft_ze_free(void *buf)
{
	if (!buf)
		return FI_SUCCESS;

	return (*libze_ops.zeMemFree)(context, buf) ? -FI_EINVAL : FI_SUCCESS;
}

int ft_ze_memset(uint64_t device, void *buf, int value, size_t size)
{
	ze_result_t ze_ret;

	ze_ret = (*libze_ops.zeCommandListReset)(cmd_list);
	if (ze_ret)
		return -FI_EINVAL;

	ze_ret = (*libze_ops.zeCommandListAppendMemoryFill)(
					cmd_list, buf, &value, sizeof(value),
					size, NULL, 0, NULL);
	if (ze_ret)
		return -FI_EINVAL;

	ze_ret = (*libze_ops.zeCommandListClose)(cmd_list);
	if (ze_ret)
		return -FI_EINVAL;

	ze_ret = (*libze_ops.zeCommandQueueExecuteCommandLists)(
					cmd_queue, 1, &cmd_list, NULL);
	if (ze_ret)
		return -FI_EINVAL;

	return FI_SUCCESS;
}

int ft_ze_copy(uint64_t device, void *dst, const void *src, size_t size)
{
	ze_result_t ze_ret;

	if (!size)
		return FI_SUCCESS;

	ze_ret = (*libze_ops.zeCommandListReset)(cmd_list);
	if (ze_ret)
		return -FI_EINVAL;

	ze_ret = (*libze_ops.zeCommandListAppendMemoryCopy)(
					cmd_list, dst, src, size, NULL, 0, NULL);
	if (ze_ret)
		return -FI_EINVAL;

	ze_ret = (*libze_ops.zeCommandListClose)(cmd_list);
	if (ze_ret)
		return -FI_EINVAL;

	ze_ret = (*libze_ops.zeCommandQueueExecuteCommandLists)(
					cmd_queue, 1, &cmd_list, NULL);
	if (ze_ret)
		return -FI_EINVAL;

	return FI_SUCCESS;
}

#else

int ft_ze_init(void)
{
	return -FI_ENOSYS;
}

int ft_ze_cleanup(void)
{
	return -FI_ENOSYS;
}

int ft_ze_alloc(uint64_t device, void **buf, size_t size)
{
	return -FI_ENOSYS;
}

int ft_ze_alloc_host(void **buffer, size_t size)
{
	return -FI_ENOSYS;
}

int ft_ze_free(void *buf)
{
	return -FI_ENOSYS;
}

int ft_ze_memset(uint64_t device, void *buf, int value, size_t size)
{
	return -FI_ENOSYS;
}

int ft_ze_copy(uint64_t device, void *dst, const void *src, size_t size)
{
	return -FI_ENOSYS;
}


#endif /* HAVE_ZE */
