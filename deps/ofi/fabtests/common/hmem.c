/*
 * Copyright (c) 2020 Intel Corporation.  All rights reserved.
 * Copyright (c) 2021 Amazon.com, Inc. or its affiliates.
 *
 * This software is available to you under the BSD license below:
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
 */

#if HAVE_CONFIG_H
	#include <config.h>
#endif

#include <inttypes.h>
#include <stdbool.h>
#include "hmem.h"

static bool hmem_initialized = false;

struct ft_hmem_ops {
	int (*init)(void);
	int (*cleanup)(void);
	int (*alloc)(uint64_t device, void **buf, size_t size);
	int (*alloc_host)(void **buf, size_t size);
	int (*free)(void *buf);
	int (*free_host)(void *buf);
	int (*mem_set)(uint64_t device, void *buf, int value, size_t size);
	int (*copy_to_hmem)(uint64_t device, void *dst, const void *src,
			    size_t size);
	int (*copy_from_hmem)(uint64_t device, void *dst, const void *src,
			      size_t size);
	int (*get_dmabuf_fd)(void *buf, size_t len,
			     int *fd, uint64_t *offset);
};

static struct ft_hmem_ops hmem_ops[] = {
	[FI_HMEM_SYSTEM] = {
		.init = ft_host_init,
		.cleanup = ft_host_cleanup,
		.alloc = ft_host_alloc,
		.alloc_host = ft_default_alloc_host,
		.free = ft_host_free,
		.free_host = ft_default_free_host,
		.mem_set = ft_host_memset,
		.copy_to_hmem = ft_host_memcpy,
		.copy_from_hmem = ft_host_memcpy,
		.get_dmabuf_fd = ft_hmem_no_get_dmabuf_fd,
	},
	[FI_HMEM_SYNAPSEAI] = {
		.init = ft_synapseai_init,
		.cleanup = ft_synapseai_cleanup,
		.alloc = ft_synapseai_alloc,
		.alloc_host = ft_synapseai_alloc_host,
		.free = ft_synapseai_free,
		.free_host = ft_synapseai_free_host,
		.mem_set = ft_synapseai_memset,
		.copy_to_hmem = ft_synapseai_copy_to_hmem,
		.copy_from_hmem = ft_synapseai_copy_from_hmem,
		.get_dmabuf_fd = ft_synapseai_get_dmabuf_fd,
	},
	[FI_HMEM_CUDA] = {
		.init = ft_cuda_init,
		.cleanup = ft_cuda_cleanup,
		.alloc = ft_cuda_alloc,
		.alloc_host = ft_cuda_alloc_host,
		.free = ft_cuda_free,
		.free_host = ft_cuda_free_host,
		.mem_set = ft_cuda_memset,
		.copy_to_hmem = ft_cuda_copy_to_hmem,
		.copy_from_hmem = ft_cuda_copy_from_hmem,
		.get_dmabuf_fd = ft_cuda_get_dmabuf_fd,
	},
	[FI_HMEM_ROCR] = {
		.init = ft_rocr_init,
		.cleanup = ft_rocr_cleanup,
		.alloc = ft_rocr_alloc,
		.alloc_host = ft_default_alloc_host,
		.free = ft_rocr_free,
		.free_host = ft_default_free_host,
		.mem_set = ft_rocr_memset,
		.copy_to_hmem = ft_rocr_memcpy,
		.copy_from_hmem = ft_rocr_memcpy,
		.get_dmabuf_fd = ft_hmem_no_get_dmabuf_fd,
	},
	[FI_HMEM_ZE] = {
		.init = ft_ze_init,
		.cleanup = ft_ze_cleanup,
		.alloc = ft_ze_alloc,
		.alloc_host = ft_ze_alloc_host,
		.free = ft_ze_free,
		.free_host = ft_ze_free,
		.mem_set = ft_ze_memset,
		.copy_to_hmem = ft_ze_copy,
		.copy_from_hmem = ft_ze_copy,
		.get_dmabuf_fd = ft_hmem_no_get_dmabuf_fd,
	},
	[FI_HMEM_NEURON] = {
		.init = ft_neuron_init,
		.cleanup = ft_neuron_cleanup,
		.alloc = ft_neuron_alloc,
		.alloc_host = ft_default_alloc_host,
		.free = ft_neuron_free,
		.free_host = ft_default_free_host,
		.mem_set = ft_neuron_memset,
		.copy_to_hmem = ft_neuron_memcpy_to_hmem,
		.copy_from_hmem = ft_neuron_memcpy_from_hmem,
		.get_dmabuf_fd = ft_hmem_no_get_dmabuf_fd,
	},
};

int ft_hmem_init(enum fi_hmem_iface iface)
{
	int ret;

	ret = hmem_ops[iface].init();
	if (ret == FI_SUCCESS)
		hmem_initialized = true;

	return ret;
}

int ft_hmem_cleanup(enum fi_hmem_iface iface)
{
	int ret = FI_SUCCESS;

	if (hmem_initialized) {
		ret = hmem_ops[iface].cleanup();
		if (ret == FI_SUCCESS)
			hmem_initialized = false;
	}

	return ret;
}

int ft_hmem_alloc(enum fi_hmem_iface iface, uint64_t device, void **buf,
		  size_t size)
{
	return hmem_ops[iface].alloc(device, buf, size);
}

int ft_default_alloc_host(void **buf, size_t size)
{
	*buf = malloc(size);
	return (*buf == NULL) ? -FI_ENOMEM : 0;
}

int ft_default_free_host(void *buf)
{
	free(buf);
	return 0;
}

int ft_hmem_alloc_host(enum fi_hmem_iface iface, void **buf,
		       size_t size)
{
	return hmem_ops[iface].alloc_host(buf, size);
}

int ft_hmem_free(enum fi_hmem_iface iface, void *buf)
{
	return hmem_ops[iface].free(buf);
}

int ft_hmem_free_host(enum fi_hmem_iface iface, void *buf)
{
	return hmem_ops[iface].free_host(buf);
}

int ft_hmem_memset(enum fi_hmem_iface iface, uint64_t device, void *buf,
		   int value, size_t size)
{
	return hmem_ops[iface].mem_set(device, buf, value, size);
}

int ft_hmem_copy_to(enum fi_hmem_iface iface, uint64_t device, void *dst,
		    const void *src, size_t size)
{
	return hmem_ops[iface].copy_to_hmem(device, dst, src, size);
}

int ft_hmem_copy_from(enum fi_hmem_iface iface, uint64_t device, void *dst,
		      const void *src, size_t size)
{
	return hmem_ops[iface].copy_from_hmem(device, dst, src, size);
}

int ft_hmem_get_dmabuf_fd(enum fi_hmem_iface iface,
			  void *buf, size_t len,
			  int *fd, uint64_t *offset)
{
	return hmem_ops[iface].get_dmabuf_fd(buf, len, fd, offset);
}

int ft_hmem_no_get_dmabuf_fd(void *buf, size_t len,
			      int *fd, uint64_t *offset)
{
	return -FI_ENOSYS;
}
