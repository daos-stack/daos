/*
 * (C) Copyright 2020 Hewlett Packard Enterprise Development LP
 * Copyright (c) 2021 Amazon.com, Inc. or its affiliates.
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

#include "hmem.h"
#include "shared.h"

#ifdef HAVE_SYNAPSEAI

#include <dlfcn.h>

#include "habanalabs/synapse_api.h"
#include "habanalabs/hlthunk.h"
#include "habanalabs/scal.h"

#define ACCEL_PAGE_SIZE 4096
struct synapseai_ops {
	synStatus (*synInitialize)(void);
	synStatus (*synDestroy)(void);
	synStatus (*synDeviceAcquireByModuleId)(synDeviceId *pDeviceId, const synModuleId moduleId);
	synStatus (*synDeviceMalloc)(const synDeviceId deviceId, const uint64_t size, uint64_t reqAddr,
				    const uint32_t flags, uint64_t *buffer);
	synStatus (*synDeviceFree)(const synDeviceId deviceId, const uint64_t buffer,
					const uint32_t flags);
	synStatus (*synStreamCreateGeneric)(synStreamHandle *pStreamHandle, const synDeviceId deviceId,
					const uint32_t flags);
	synStatus (*synStreamSynchronize)(const synStreamHandle streamHandle);
	synStatus (*synHostMalloc)(const synDeviceId deviceId, const uint64_t size,
					const uint32_t flags, void **buffer);
	synStatus (*synHostFree)(const synDeviceId deviceId, const void *buffer, const uint32_t flags);
	synStatus (*synMemsetD8Async)(uint64_t pDeviceMem, const unsigned char value,
					const size_t numOfElements, const synStreamHandle streamHandle);
	synStatus (*synMemCopyAsync)(const synStreamHandle streamHandle, const uint64_t src,
					const uint64_t size, const uint64_t dst, const synDmaDir direction);
	synStatus (*synDeviceGetInfoV2)(const synDeviceId deviceId, synDeviceInfoV2 *pDeviceInfo);
	int (*hlthunk_device_mapped_memory_export_dmabuf_fd)(int fd, uint64_t addr, uint64_t size,
					uint64_t offset, uint32_t flags);
	int (*scal_pool_get_infoV2)(const scal_pool_handle_t pool, scal_memory_pool_infoV2 *info);
	int (*scal_get_pool_handle_by_name)(const scal_handle_t scal, const char *pool_name,
					scal_pool_handle_t *pool);
	int (*scal_get_handle_from_fd)(int fd, scal_handle_t *scal);
};

static void *synapseai_handle;
static void *hlthunk_handle;
static void *scal_handle;
static struct synapseai_ops synapseai_ops;
static synDeviceId synapseai_fd = -1;
static synStreamHandle synapseai_stream_handle;
static synDeviceInfoV2 deviceInfo;
static uint64_t device_fd;

static void cleanup_synapseai_ops(void)
{
	if (synapseai_handle) {
		dlclose(synapseai_handle);
		synapseai_handle = NULL;
	}

	if (hlthunk_handle) {
		dlclose(hlthunk_handle);
		hlthunk_handle = NULL;
	}

	if (scal_handle) {
		dlclose(scal_handle);
		scal_handle = NULL;
	}
}

int init_synapseai_ops(void)
{
	synapseai_handle = dlopen("libSynapse.so", RTLD_NOW);
	if (!synapseai_handle) {
		FT_ERR("Failed to dlopen libSynapse.so\n");
		goto err_out;
	}

	synapseai_ops.synInitialize = dlsym(synapseai_handle, "synInitialize");
	if (!synapseai_ops.synInitialize) {
		FT_ERR("Failed to find synInitialize\n");
		goto err_dlclose;
	}

	synapseai_ops.synDestroy = dlsym(synapseai_handle, "synDestroy");
	if (!synapseai_ops.synDestroy) {
		FT_ERR("Failed to find synDestroy\n");
		goto err_dlclose;
	}

	synapseai_ops.synDeviceAcquireByModuleId = dlsym(synapseai_handle,
													 "synDeviceAcquireByModuleId");
	if (!synapseai_ops.synDeviceAcquireByModuleId) {
		FT_ERR("Failed to find synDeviceAcquireByModuleId\n");
		goto err_dlclose;
	}

	synapseai_ops.synDeviceMalloc = dlsym(synapseai_handle, "synDeviceMalloc");
	if (!synapseai_ops.synDeviceMalloc) {
		FT_ERR("Failed to find synDeviceMalloc\n");
		goto err_dlclose;
	}

	synapseai_ops.synDeviceFree = dlsym(synapseai_handle, "synDeviceFree");
	if (!synapseai_ops.synDeviceFree) {
		FT_ERR("Failed to find synDeviceFree\n");
		goto err_dlclose;
	}

	synapseai_ops.synStreamCreateGeneric = dlsym(synapseai_handle, "synStreamCreateGeneric");
	if (!synapseai_ops.synStreamCreateGeneric) {
		FT_ERR("Failed to find synStreamCreateGeneric\n");
		goto err_dlclose;
	}

	synapseai_ops.synStreamSynchronize = dlsym(synapseai_handle, "synStreamSynchronize");
	if (!synapseai_ops.synStreamSynchronize) {
		FT_ERR("Failed to find synStreamSynchronize\n");
		goto err_dlclose;
	}

	synapseai_ops.synHostMalloc = dlsym(synapseai_handle, "synHostMalloc");
	if (!synapseai_ops.synHostMalloc) {
		FT_ERR("Failed to find synHostMalloc\n");
		goto err_dlclose;
	}

	synapseai_ops.synHostFree = dlsym(synapseai_handle, "synHostFree");
	if (!synapseai_ops.synHostFree) {
		FT_ERR("Failed to find synHostFree\n");
		goto err_dlclose;
	}

	synapseai_ops.synMemsetD8Async = dlsym(synapseai_handle, "synMemsetD8Async");
	if (!synapseai_ops.synMemsetD8Async) {
		FT_ERR("Failed to find synMemsetD8Async\n");
		goto err_dlclose;
	}

	synapseai_ops.synMemCopyAsync = dlsym(synapseai_handle, "synMemCopyAsync");
	if (!synapseai_ops.synMemCopyAsync) {
		FT_ERR("Failed to find synMemCopyAsync\n");
		goto err_dlclose;
	}

	synapseai_ops.synDeviceGetInfoV2 = dlsym(synapseai_handle, "synDeviceGetInfoV2");
	if (!synapseai_ops.synDeviceGetInfoV2) {
		FT_ERR("Failed to find synDeviceGetInfoV2\n");
		goto err_dlclose;
	}

	hlthunk_handle = dlopen("libhl-thunk.so", RTLD_NOW);
	if (!hlthunk_handle) {
		FT_ERR("Failed to dlopen libhl-thunk.so\n");
		goto err_dlclose;
	}

	synapseai_ops.hlthunk_device_mapped_memory_export_dmabuf_fd =
		dlsym(hlthunk_handle, "hlthunk_device_mapped_memory_export_dmabuf_fd");
	if (!synapseai_ops.hlthunk_device_mapped_memory_export_dmabuf_fd) {
		FT_ERR("Failed to find hlthunk_device_mapped_memory_export_dmabuf_fd\n");
		goto err_dlclose;
	}

	scal_handle = dlopen("libscal.so", RTLD_NOW);
	if (!scal_handle) {
		FT_ERR("Falid to dlopen libscal.so\n");
		goto err_dlclose;
	}

	synapseai_ops.scal_pool_get_infoV2 = dlsym(scal_handle, "scal_pool_get_infoV2");
	if (!synapseai_ops.scal_pool_get_infoV2) {
		FT_ERR("Failed to find scal_pool_get_infoV2\n");
		goto err_dlclose;
	}

	synapseai_ops.scal_get_pool_handle_by_name =
		dlsym(scal_handle, "scal_get_pool_handle_by_name");
	if (!synapseai_ops.scal_get_pool_handle_by_name) {
		FT_ERR("Failed to find scal_get_pool_handle_by_name\n");
		goto err_dlclose;
	}

	synapseai_ops.scal_get_handle_from_fd = dlsym(scal_handle, "scal_get_handle_from_fd");
	if (!synapseai_ops.scal_get_handle_from_fd) {
		FT_ERR("Failed to find scal_get_handle_from_fd\n");
		goto err_dlclose;
	}

	return FI_SUCCESS;

err_dlclose:
	cleanup_synapseai_ops();

err_out:
	return -FI_ENODATA;
}

int stream_synchronize(const synStreamHandle streamHandle)
{
	if (synapseai_ops.synStreamSynchronize(synapseai_stream_handle) != synSuccess) {
		return -FI_ENOBUFS;
	}
	return FI_SUCCESS;
}

int ft_synapseai_init(void)
{
	if (setenv("MLX5_SCATTER_TO_CQE", "0", 1) != 0) {
		FT_ERR("Failed to set MLX5_SCATTER_TO_CQE environment variable\n");
		return -FI_ENOSYS;
	}

	if (synapseai_handle)
		return FI_SUCCESS;

	if (init_synapseai_ops())
		return -FI_ENODATA;

	if (synapseai_ops.synInitialize() != synSuccess) {
		FT_ERR("Failed to synInitialize()\n");
		goto err;
	}

	if (synapseai_ops.synDeviceAcquireByModuleId(&synapseai_fd, opts.device) != synSuccess) {
		FT_ERR("Failed to synDeviceAcquireByModuleId()\n");
		goto err;
	}

	if (synapseai_ops.synDeviceGetInfoV2(synapseai_fd, &deviceInfo) != synSuccess) {
		FT_ERR("Failed to synDeviceGetInfoV2()\n");
		goto err;
	}
	device_fd = deviceInfo.fd;

	if (synapseai_ops.synStreamCreateGeneric(&synapseai_stream_handle, synapseai_fd, 0) !=
		synSuccess) {
		FT_ERR("Failed to synStreamCreateGeneric()\n");
		goto err;
	}

	FT_DEBUG("Successfully initialized Synapseai");
	return FI_SUCCESS;

err:
	cleanup_synapseai_ops();
	return -FI_ENODATA;
}

int ft_synapseai_cleanup(void)
{
	if (synapseai_fd != -1) {
		synapseai_ops.synDestroy();
	}
	cleanup_synapseai_ops();
	return 0;
}

int ft_synapseai_alloc(uint64_t device, void **buf, size_t size)
{
	uint64_t addr;
	size_t buf_size = (size + ACCEL_PAGE_SIZE - 1) & ~(ACCEL_PAGE_SIZE - 1);

	if (synapseai_ops.synDeviceMalloc(synapseai_fd, buf_size, 0x0, 0, &addr) != synSuccess) {
		FT_ERR("synDeviceMalloc failed");
		return -FI_ENOBUFS;
	}

	if (addr == 0) {
		FT_ERR("synDeviceMalloc returned invalid address");
		return -FI_ENOBUFS;
	}
	*buf = (void *)addr;
	return 0;
}

int ft_synapseai_free(void *buf)
{
	if (synapseai_ops.synDeviceFree(synapseai_fd, (uint64_t)buf, 0) != synSuccess) {
		return -FI_ENOBUFS;
	}
	return 0;
}

int ft_synapseai_memset(uint64_t device, void *buf, int value, size_t size)
{
	if (synapseai_ops.synMemsetD8Async((uint64_t)buf, (unsigned char)value, size,
										synapseai_stream_handle) != synSuccess) {
		return -FI_ENOBUFS;
	}

	return stream_synchronize(synapseai_stream_handle);
}

int ft_synapseai_alloc_host(void **buf, size_t size)
{
	size_t buf_size = (size + ACCEL_PAGE_SIZE - 1) & ~(ACCEL_PAGE_SIZE - 1);

	if (synapseai_ops.synHostMalloc(synapseai_fd, buf_size, 0, buf) != synSuccess) {
		return -FI_ENOBUFS;
	}
	return FI_SUCCESS;
}

int ft_synapseai_free_host(void *buf)
{
	if (synapseai_ops.synHostFree(synapseai_fd, buf, 0) != synSuccess) {
		return -FI_ENOBUFS;
	}
	return FI_SUCCESS;
}

int ft_synapseai_copy_to_hmem(uint64_t device, void *dst, const void *src, size_t size)
{
	if (synapseai_ops.synMemCopyAsync(synapseai_stream_handle, (uint64_t)src, size, (uint64_t)dst,
									  HOST_TO_DRAM) != synSuccess) {
		return -FI_ENOBUFS;
	}
	return stream_synchronize(synapseai_stream_handle);
}

int ft_synapseai_copy_from_hmem(uint64_t device, void *dst, const void *src, size_t size)
{
	if (synapseai_ops.synMemCopyAsync(synapseai_stream_handle, (uint64_t)src, size, (uint64_t)dst,
									  DRAM_TO_HOST) != synSuccess) {
		return -FI_ENOBUFS;
	}
	return stream_synchronize(synapseai_stream_handle);
}

int ft_synapseai_get_dmabuf_fd(void *buf, size_t len, int *dmabuf_fd, uint64_t *dmabuf_offset)
{
	scal_pool_handle_t mpHandle;
	scal_memory_pool_infoV2 mpInfo;
	scal_handle_t a = 0;

	if (synapseai_ops.scal_get_handle_from_fd(device_fd, &a) != SCAL_SUCCESS) {
		return -FI_ENOBUFS;
	}

	if (synapseai_ops.scal_get_pool_handle_by_name(a, "global_hbm", &mpHandle) != SCAL_SUCCESS) {
		return -FI_ENOBUFS;
	}

	if (synapseai_ops.scal_pool_get_infoV2(mpHandle, &mpInfo) != SCAL_SUCCESS) {
		return -FI_ENOBUFS;
	}
	uint64_t baseAddress = mpInfo.device_base_allocated_address;

	size_t buf_size = (len + ACCEL_PAGE_SIZE - 1) & ~(ACCEL_PAGE_SIZE - 1);
	*dmabuf_fd =
		synapseai_ops.hlthunk_device_mapped_memory_export_dmabuf_fd(device_fd,
				baseAddress,
				buf_size,
				(uint64_t)buf - baseAddress,
				(O_RDWR | O_CLOEXEC));

	if (*dmabuf_fd < 0)	{
		FT_ERR("Failed to export synapseai dmabuf\n");
		return -FI_ENOBUFS;
	}
	*dmabuf_offset = 0;
	return FI_SUCCESS;
}

#else

int ft_synapseai_init(void)
{
	return -FI_ENOSYS;
}

int ft_synapseai_cleanup(void)
{
	return -FI_ENOSYS;
}

int ft_synapseai_alloc(uint64_t device, void **buf, size_t size)
{
	return -FI_ENOSYS;
}

int ft_synapseai_alloc_host(void **buf, size_t size)
{
	return -FI_ENOSYS;
}

int ft_synapseai_free(void *buf)
{
	return -FI_ENOSYS;
}

int ft_synapseai_free_host(void *buf)
{
	return -FI_ENOSYS;
}

int ft_synapseai_memset(uint64_t device, void *buf, int value, size_t size)
{
	return -FI_ENOSYS;
}

int ft_synapseai_copy_to_hmem(uint64_t device, void *dst, const void *src, size_t size)
{
	return -FI_ENOSYS;
}

int ft_synapseai_copy_from_hmem(uint64_t device, void *dst, const void *src, size_t size)
{
	return -FI_ENOSYS;
}

int ft_synapseai_get_dmabuf_fd(void *buf, size_t len, int *dmabuf_fd, uint64_t *dmabuf_offset)
{
	return -FI_ENOSYS;
}
#endif /*_HAVE_SYNAPSEAI_H */
