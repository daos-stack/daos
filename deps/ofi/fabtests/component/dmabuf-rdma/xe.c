/*
 * Copyright (c) 2021-2022 Intel Corporation.  All rights reserved.
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
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <level_zero/ze_api.h>
#include "util.h"
#include "xe.h"

/*
 * Memory allocation & copy routines using oneAPI L0
 */

#define MAX_GPUS	(8)

extern int buf_location;

struct gpu {
	int dev_num;
	int subdev_num;
	ze_device_handle_t device;
	ze_command_list_handle_t cmdl;
};

int use_dmabuf_reg;

static int num_gpus;
static struct gpu gpus[MAX_GPUS];
static ze_driver_handle_t gpu_driver;
static ze_context_handle_t gpu_context;

static int init_gpu(int gpu, int dev_num, int subdev_num)
{
	uint32_t count;
	ze_device_handle_t *devices;
	ze_device_handle_t device;
	ze_command_queue_desc_t cq_desc = {
	    .stype = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC,
	    .ordinal = 0,
	    .index = 0,
	    .flags = 0,
	    .mode = ZE_COMMAND_QUEUE_MODE_SYNCHRONOUS,
	    .priority = ZE_COMMAND_QUEUE_PRIORITY_NORMAL,
	};
	ze_command_list_handle_t cmdl;

	count = 0;
	EXIT_ON_ERROR(libze_ops.zeDeviceGet(gpu_driver, &count, NULL));

	if (count < dev_num + 1) {
		fprintf(stderr, "GPU device %d does't exist\n", dev_num);
		goto err_out;
	}

	devices = calloc(count, sizeof(*devices));
	if (!devices) {
		perror("calloc");
		goto err_out;
	}

	EXIT_ON_ERROR(libze_ops.zeDeviceGet(gpu_driver, &count, devices));
	device = devices[dev_num];
	free(devices);

	if (subdev_num >= 0) {
		count = 0;
		EXIT_ON_ERROR(libze_ops.zeDeviceGetSubDevices(device, &count, NULL));

		if (count < subdev_num + 1) {
			fprintf(stderr, "GPU subdevice %d.%d does't exist\n",
				dev_num, subdev_num);
			goto err_out;
		}

		devices = calloc(count, sizeof(*devices));
		if (!devices) {
			perror("calloc");
			goto err_out;
		}

		EXIT_ON_ERROR(libze_ops.zeDeviceGetSubDevices(device, &count, devices));
		device = devices[subdev_num];
		free(devices);

		printf("using GPU subdevice %d.%d: %p (total %d)\n", dev_num,
			subdev_num, device, count);
	} else {
		printf("using GPU device %d: %p (total %d)\n", dev_num,
			device, count);
	}

	EXIT_ON_ERROR(libze_ops.zeCommandListCreateImmediate(gpu_context, device,
						   &cq_desc, &cmdl));

	gpus[gpu].dev_num = dev_num;
	gpus[gpu].subdev_num = subdev_num;
	gpus[gpu].device = device;
	gpus[gpu].cmdl = cmdl;
	return 0;

err_out:
	return -1;
}

int xe_init(char *gpu_dev_nums, int enable_multi_gpu)
{
	uint32_t count;
	ze_context_desc_t ctxt_desc = {};
	char *gpu_dev_num;
	char *s;
	int dev_num, subdev_num;
	char *saveptr;

	EXIT_ON_ERROR(init_libze_ops());
	EXIT_ON_ERROR(libze_ops.zeInit(ZE_INIT_FLAG_GPU_ONLY));

	count = 1;
	EXIT_ON_ERROR(libze_ops.zeDriverGet(&count, &gpu_driver));
	printf("Using first driver: %p (total >= %d)\n", gpu_driver, count);

	EXIT_ON_ERROR(libze_ops.zeContextCreate(gpu_driver, &ctxt_desc,
						&gpu_context));

	num_gpus = 0;
	if (gpu_dev_nums) {
		gpu_dev_num = strtok_r(gpu_dev_nums, ",", &saveptr);
		while (gpu_dev_num && num_gpus < MAX_GPUS) {
			dev_num = 0;
			subdev_num = -1;
			dev_num = atoi(gpu_dev_num);
			s = strchr(gpu_dev_num, '.');
			if (s)
				subdev_num = atoi(s + 1);
			gpu_dev_num = strtok_r(NULL, ",", &saveptr);

			if (init_gpu(num_gpus, dev_num, subdev_num) < 0)
				continue;

			num_gpus++;

			if (!enable_multi_gpu)
				break;
		}
	} else {
		if (!init_gpu(0, 0, -1))
			num_gpus++;
	}

	return num_gpus;
}

int xe_get_dev_num(int i)
{
	if (i < 0 || i >= num_gpus)
		return -1;

	return gpus[i].dev_num;
}

void xe_show_buf(struct xe_buf *buf)
{
	printf("Allocation: buf %p alloc_base %p alloc_size %ld offset 0x%lx type %d device %p\n",
		buf->buf, buf->base, buf->size, buf->offset, buf->type, buf->dev);
}

int xe_get_buf_fd(void *buf)
{
	ze_ipc_mem_handle_t ipc;

	memset(&ipc, 0, sizeof(ipc));
	CHECK_ERROR((libze_ops.zeMemGetIpcHandle(gpu_context, buf, &ipc)));

	return (int)*(uint64_t *)&ipc;

err_out:
	return -1;
}

void *xe_alloc_buf(size_t page_size, size_t size, int where, int gpu,
		   struct xe_buf *xe_buf)
{
	void *buf = NULL;
	ze_device_mem_alloc_desc_t dev_desc = {};
	ze_host_mem_alloc_desc_t host_desc = {};
	ze_memory_allocation_properties_t alloc_props = { .type = -1};
	ze_device_handle_t alloc_dev = 0;
	void *alloc_base;
	size_t alloc_size;

	switch (where) {
	  case MALLOC:
		posix_memalign(&buf, page_size, size);
		alloc_base = buf;
		alloc_size = size;
		break;
	  case HOST:
		EXIT_ON_ERROR(libze_ops.zeMemAllocHost(gpu_context, &host_desc, size,
					     page_size, &buf));
		break;
	  case DEVICE:
		EXIT_ON_ERROR(libze_ops.zeMemAllocDevice(gpu_context, &dev_desc, size,
					       page_size, gpus[gpu].device, &buf));
		break;
	  default:
		EXIT_ON_ERROR(libze_ops.zeMemAllocShared(gpu_context, &dev_desc,
					       &host_desc, size, page_size,
					       gpus[gpu].device, &buf));
		break;
	}

	if (where != MALLOC) {
		EXIT_ON_ERROR(libze_ops.zeMemGetAllocProperties(gpu_context, buf,
						      &alloc_props, &alloc_dev));
		EXIT_ON_ERROR(libze_ops.zeMemGetAddressRange(gpu_context, buf, &alloc_base,
						   &alloc_size));
		if (use_dmabuf_reg)
			EXIT_ON_ERROR(dmabuf_reg_add((uintptr_t)alloc_base,
						     alloc_size,
						     xe_get_buf_fd(buf)));
	}

	if (xe_buf) {
		xe_buf->buf = buf;
		xe_buf->base = alloc_base;
		xe_buf->size = alloc_size;
		xe_buf->offset = (char *)buf - (char *)alloc_base;
		xe_buf->type = alloc_props.type;
		xe_buf->dev = alloc_dev;
		xe_buf->location = where;
		xe_show_buf(xe_buf);
	}
	return buf;
}

void xe_free_buf(void *buf, int where)
{
	if (where == MALLOC) {
		free(buf);
	} else {
		if (use_dmabuf_reg)
			dmabuf_reg_remove((uint64_t)buf);
		CHECK_ERROR(libze_ops.zeMemFree(gpu_context, buf));
	}
err_out:
	return;
}

void xe_set_buf(void *buf, char c, size_t size, int location, int gpu)
{
	if (location == MALLOC) {
		memset(buf, c, size);
	} else {
		EXIT_ON_ERROR(libze_ops.zeCommandListAppendMemoryFill(gpus[gpu].cmdl,
					  buf, &c, 1, size, NULL, 0, NULL));
		EXIT_ON_ERROR(libze_ops.zeCommandListReset(gpus[gpu].cmdl));
	}
}

void xe_copy_buf(void *dst, void *src, size_t size, int gpu)
{
	EXIT_ON_ERROR(libze_ops.zeCommandListAppendMemoryCopy(
				  gpus[gpu].cmdl, dst, src, size, NULL, 0, NULL));
	EXIT_ON_ERROR(libze_ops.zeCommandListReset(gpus[gpu].cmdl));
}
