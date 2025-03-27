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

/*
 *	memcopy-xe.c
 *
 *	This is a memory copy bandwidth test with buffers allocated via oneAPI L0
 *	functions.
 *
 *	Copy using memcpy() between buffers allocated with malloc():
 *
 *	    ./xe_memcopy -c memcpy M M
 *
 *	Copy using memcpy between buffers allocated with zeMemAllocHost():
 *
 *	    ./xe_memcopy -c memcpy H H
 *
 *	Copy using GPU between buffers allocated with zeMemAllocDevice():
 *
 *	    ./xe_memcopy -c cmdq D D
 *
 *	Copy using GPU from buffer allocated with zeMemAllocDevice() to buffer
 *	allcated with zeMemAllocHost():
 *
 *	    ./xe_memcopy -c cmdq D H
 *
 *	Copy using GPU between buffers allocated with zeMemAllocDevice(), with
 *	cached command list:
 *
 *	    ./xe_memcopy -c cmdq -C D D
 *
 *	Copy using GPU between buffers allocated with zeMemAllocDevice(), with
 *	immediate command list:
 *
 *	    ./xe_memcopy -c cmdq -i D D
 *
 *	Copy using GPU between buffers allocated with zeMemAllocDevice(), using
 *	specified device (src=dst=0):
 *
 *	    ./xe_memcopy -c cmdq -d 0 D D
 *
 *	Copy using GPU between buffers allocated with zeMemAllocDevice(), using
 *	specified device (src=0, dst=1):
 *
 *	    ./xe_memcopy -c cmdq -d 0 -D 1 D D
 *
 *	Copy using GPU between buffers allocated with zeMemAllocDevice(), using
 *	specified command queue group:
 *
 *	    ./xe_memcopy -c cmdq -G 2 D D
 *
 *	Copy using GPU between buffers allocated with zeMemAllocDevice(), using
 *	specified devices, command queue group and engine index:
 *
 *	    ./xe_memcopy -c cmdq -d 0 -D 1 -G 2 -I 1 D D
 *
 *	Copy using GPU between buffers allocated with zeMemAllocDevice(), using
 *	specified devices, 2nd device's command queue group and engine index:
 *
 *	    ./xe_memcopy -c cmdq -d 0 -D 1 -r -G 2 -I 1 D D
 *
 *	Copy using mmap + memcpy between buffers allocated with zeMemAllocDevice(),
 *	mmap() is called before each memcpy():
 *
 *	    ./xe_memcopy -c mmap D D
 *
 *	Copy using mmap + memcpy between buffers allocated with zeMemAllocDevice(),
 *	mmap() is called once (cached):
 *
 *	    ./xe_memcopy -c mmap -C D D
 *
 *	For more options:
 *
 *	    ./xe_memcopy -h
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <setjmp.h>
#include <level_zero/ze_api.h>
#include "util.h"
#include "xe.h"

#define MALLOC	0
#define HOST	1
#define DEVICE	2
#define SHARED	3

struct my_buf {
	int where;
	void *buf;
	size_t size;
	int fd;
	void *map;
	size_t map_size;
};

const size_t gpu_page_size = 65536;
static int iterations = 1000;

#define CMDQ		0
#define MEMCPY		1
#define MMAP		2

static ze_result_t (*zexDriverImportExternalPointer)(ze_driver_handle_t drv,
						     void *ptr, size_t len);
static ze_result_t (*zexDriverReleaseImportedPointer)(ze_driver_handle_t drv,
						      void *ptr);

static int copy_method = CMDQ;
static int use_imm_cmdl = 0;
static int cache_cmdl = 0;
static int cache_mmap = 0;
static int mmap_all = 1;
static int reverse = 0;
static int import = 0;
static unsigned int card_num = 0;
static unsigned int card_num2 = 0xFF;
static unsigned int ordinal = 0;
static unsigned int engine_index = 0;

#define MAX_MSG_SIZE	(64*1024*1024)

static jmp_buf env;

void segv_handler(int signum)
{
	printf("Segmentation fault caught while accessing device buffer\n");
	longjmp(env, 1);
}

static struct {
	ze_driver_handle_t	drv;
	ze_context_handle_t	ctxt;
	ze_device_handle_t	dev;
	ze_device_handle_t	dev2;
	ze_command_queue_handle_t cmdq;
	ze_command_list_handle_t cmdl;
} gpu;

void show_device_properties(ze_device_properties_t *props)
{
	printf("vendor_id: 0x%x, device_id: 0x%x, name: %s, type: 0x%x, flags: 0x%x\n",
		props->vendorId, props->deviceId, props->name, props->type,
		props->flags);

	printf("\tsubdevice_id: 0x%x, core_clock: %d, max_mem_alloc: %ld, "
	       "max_hw_ctxts: %d, threads_per_EU: %d, slices: %d\n",
		props->subdeviceId, props->coreClockRate,
		props->maxMemAllocSize, props->maxHardwareContexts,
		props->numThreadsPerEU, props->numSlices);
}

void show_cmdq_group_info(ze_device_handle_t dev)
{
	ze_command_queue_group_properties_t *props;
	uint32_t cnt = 0;
	int i;

	EXIT_ON_ERROR(libze_ops.zeDeviceGetCommandQueueGroupProperties(dev, &cnt, NULL));

	props = calloc(cnt, sizeof(ze_command_queue_group_properties_t));
	if (!props) {
		printf("Error: cannot allocate memory for cmdq_group_properties\n");
		exit(-1);
	}

	EXIT_ON_ERROR(libze_ops.zeDeviceGetCommandQueueGroupProperties(dev, &cnt, props));

	printf("\tcommand queue groups: ");

	for(i = 0; i < cnt; ++i)
		printf("%d:[%s%s]x%d%s", i,
			(props[i].flags & ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_COMPUTE) ? "comp," : "",
			(props[i].flags & ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_COPY) ? "copy" : "",
			props[i].numQueues, i < cnt -1 ? ", " : "\n");
}

void init_gpu(void)
{
	uint32_t count;
	ze_device_handle_t *all_devices;
	ze_device_properties_t device_properties;
	ze_context_desc_t ctxt_desc = {};
	ze_command_queue_desc_t cq_desc = {
		.stype = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC,
		.ordinal = ordinal,
		.index = engine_index,
		.flags = 0,
		.mode = use_imm_cmdl ? ZE_COMMAND_QUEUE_MODE_SYNCHRONOUS :
				       ZE_COMMAND_QUEUE_MODE_DEFAULT,
		.priority = ZE_COMMAND_QUEUE_PRIORITY_NORMAL,
	};
	ze_command_list_desc_t cl_desc = {
		.stype = ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC,
		.commandQueueGroupOrdinal = ordinal,
		.flags = 0,
	};
	ze_bool_t succ;
	ze_device_handle_t cmdq_dev;

	EXIT_ON_ERROR(init_libze_ops());
	EXIT_ON_ERROR(libze_ops.zeInit(ZE_INIT_FLAG_GPU_ONLY));

	/* get the first driver */
	count = 1;
	EXIT_ON_ERROR(libze_ops.zeDriverGet(&count, &gpu.drv));

	if (import) {
		EXIT_ON_ERROR(libze_ops.zeDriverGetExtensionFunctionAddress(
				gpu.drv,
				"zexDriverImportExternalPointer",
				(void *)&zexDriverImportExternalPointer));
		EXIT_ON_ERROR(libze_ops.zeDriverGetExtensionFunctionAddress(
				gpu.drv,
				"zexDriverReleaseImportedPointer",
				(void *)&zexDriverReleaseImportedPointer));
	}

	/* get the number of GPU devices */
	count = 0;
	EXIT_ON_ERROR(libze_ops.zeDeviceGet(gpu.drv, &count, NULL));
	printf("Total number of devices: %d\n", count);

	if (card_num >= count || card_num2 >= count) {
		printf("Error: card number exeeds available devices (%d)\n",
			count);
		exit(-1);
	}

	/* get GPU devices */
	all_devices = calloc(count, sizeof(*all_devices));
	if (!all_devices) {
		printf("Error: cannot allocate memory for all_devices\n");
		exit(-1);
	}

	EXIT_ON_ERROR(libze_ops.zeDeviceGet(gpu.drv, &count, all_devices));
	gpu.dev = all_devices[card_num];
	gpu.dev2 = all_devices[card_num2];

	printf("Use device %d: %p, ", card_num, gpu.dev);
	libze_ops.zeDeviceGetProperties(gpu.dev, &device_properties);
	show_device_properties(&device_properties);
	show_cmdq_group_info(gpu.dev);

	if (card_num2 != card_num) {
		printf("Use device %d: %p, ", card_num2, gpu.dev2);
		libze_ops.zeDeviceGetProperties(gpu.dev2, &device_properties);
		show_device_properties(&device_properties);
		show_cmdq_group_info(gpu.dev2);
	}

	/* check peer access capability */
	EXIT_ON_ERROR(libze_ops.zeDeviceCanAccessPeer(gpu.dev, gpu.dev2, &succ));
	printf("Peer access from device %d to device %d is %s\n",
		card_num, card_num2, succ ? "supported" : "unsupported");

	/* create a context */
	EXIT_ON_ERROR(libze_ops.zeContextCreate(gpu.drv, &ctxt_desc, &gpu.ctxt));

	/* create command queue and commad list */
	cmdq_dev = reverse ? gpu.dev2 : gpu.dev;
	if (use_imm_cmdl) {
		EXIT_ON_ERROR(libze_ops.zeCommandListCreateImmediate(gpu.ctxt, cmdq_dev,
							   &cq_desc,
							   &gpu.cmdl));
	} else {
		EXIT_ON_ERROR(libze_ops.zeCommandQueueCreate(gpu.ctxt, cmdq_dev,
						   &cq_desc, &gpu.cmdq));
		EXIT_ON_ERROR(libze_ops.zeCommandListCreate(gpu.ctxt, cmdq_dev,
						  &cl_desc, &gpu.cmdl));
	}
}

void finalize_gpu(void)
{
	EXIT_ON_ERROR(libze_ops.zeCommandListDestroy(gpu.cmdl));
	if (!use_imm_cmdl)
		EXIT_ON_ERROR(libze_ops.zeCommandQueueDestroy(gpu.cmdq));
}

int get_buf_fd(void *buf)
{
	ze_ipc_mem_handle_t ipc;

	memset(&ipc, 0, sizeof(ipc));
	EXIT_ON_ERROR((libze_ops.zeMemGetIpcHandle(gpu.ctxt, buf, &ipc)));

	return (int)*(uint64_t *)&ipc;
}

void alloc_buffer(struct my_buf *buf, size_t size, ze_device_handle_t dev)
{
	ze_device_mem_alloc_desc_t dev_desc = {};
	ze_host_mem_alloc_desc_t host_desc = {};

	switch (buf->where) {
	  case MALLOC:
		EXIT_ON_ERROR(posix_memalign(&buf->buf, 4096, size));
		if (import)
			EXIT_ON_ERROR((*zexDriverImportExternalPointer)(gpu.drv,
									buf->buf,
									size));
		break;
	  case HOST:
		EXIT_ON_ERROR(libze_ops.zeMemAllocHost(gpu.ctxt, &host_desc, size, 4096,
					     &buf->buf));
		break;
	  case DEVICE:
		EXIT_ON_ERROR(libze_ops.zeMemAllocDevice(gpu.ctxt, &dev_desc, size, 4096,
					       dev, &buf->buf));
		break;
	  default:
		EXIT_ON_ERROR(libze_ops.zeMemAllocShared(gpu.ctxt, &dev_desc, &host_desc,
					       size, 4096, dev, &buf->buf));
		break;
	}

	buf->size = size;
	buf->fd = -1;
	buf->map = NULL;
	buf->map_size = (size + 4095) & ~4095UL;

	if (buf->where == MALLOC)
		return;

	buf->fd = get_buf_fd(buf->buf);

	if (cache_mmap && copy_method == MMAP &&
	    (buf->where == DEVICE || mmap_all)) {
		buf->map = mmap(NULL, buf->map_size, PROT_READ | PROT_WRITE,
				MAP_SHARED, buf->fd, 0);
		if (buf->map == MAP_FAILED) {
			perror("mmap");
			exit(-1);
		}
	}
}

void free_buffer(struct my_buf *buf)
{
	if (buf->map)
		munmap(buf->map, buf->map_size);

	if (buf->fd != -1)
		close(buf->fd);

	if (buf->where == MALLOC && import)
		EXIT_ON_ERROR((*zexDriverReleaseImportedPointer)(gpu.drv,
								 buf->buf));

	if (buf->where == MALLOC)
		free(buf->buf);
	else
		EXIT_ON_ERROR(libze_ops.zeMemFree(gpu.ctxt, buf->buf));
}

void *get_buf_ptr(struct my_buf *buf, size_t size)
{
	if (buf->map)
		return buf->map;

	if (copy_method == MMAP && buf->where != MALLOC) {
		buf->map = mmap(NULL, buf->map_size, PROT_READ | PROT_WRITE,
				MAP_SHARED, buf->fd, 0);
		if (buf->map == MAP_FAILED) {
			perror("mmap");
			exit(-1);
		}
		return buf->map;
	}

	return buf->buf;
}

void put_buf_ptr(struct my_buf *buf)
{
	if (!cache_mmap && copy_method == MMAP && buf->where != MALLOC) {
		munmap(buf->map, buf->map_size);
		buf->map = NULL;
	}
}

void copy_buffer(struct my_buf *src_buf, struct my_buf *dst_buf, size_t size)
{
	if (copy_method == MEMCPY) {
		memcpy(dst_buf->buf, src_buf->buf, size);
	} else if (copy_method == MMAP) {
		memcpy(get_buf_ptr(dst_buf, size),
		       get_buf_ptr(src_buf, size), size);
		put_buf_ptr(src_buf);
		put_buf_ptr(dst_buf);
	} else if (copy_method == CMDQ) {
		if (use_imm_cmdl) {
			EXIT_ON_ERROR(libze_ops.zeCommandListAppendMemoryCopy(
						gpu.cmdl, dst_buf->buf,
						src_buf->buf, size, NULL, 0,
						NULL));
			EXIT_ON_ERROR(libze_ops.zeCommandListReset(gpu.cmdl));
		} else {
			if (!cache_cmdl) {
				EXIT_ON_ERROR(libze_ops.zeCommandListAppendMemoryCopy(
						gpu.cmdl, dst_buf->buf,
						src_buf->buf, size, NULL, 0,
						NULL));
				EXIT_ON_ERROR(libze_ops.zeCommandListClose(gpu.cmdl));
			}
			EXIT_ON_ERROR(libze_ops.zeCommandQueueExecuteCommandLists(
						gpu.cmdq, 1, &gpu.cmdl, NULL));
			EXIT_ON_ERROR(libze_ops.zeCommandQueueSynchronize(
						gpu.cmdq, UINT32_MAX));
			if (!cache_cmdl)
				EXIT_ON_ERROR(libze_ops.zeCommandListReset(gpu.cmdl));
		}
	}
}

void fill_buffer(struct my_buf *buf, char c, size_t size)
{
	void *mapped;

	if (buf->where != DEVICE) {
		memset(buf->buf, c, size);
	} else {
		mapped = mmap(NULL, buf->map_size, PROT_READ | PROT_WRITE,
			      MAP_SHARED, buf->fd, 0);
		if (mapped == MAP_FAILED) {
			perror("mmap");
			exit(-1);
		}
		memset(mapped, c, size);
		munmap(mapped, buf->map_size);
	}
}

void check_buffer(struct my_buf *buf, char c, size_t size)
{
	char *p;
	int i;
	size_t errors = 0;

	if (buf->where != DEVICE) {
		p = buf->buf;
		for (i = 0; i< size; i++)
			if (p[i] != c) errors++;
	} else {
		p = mmap(NULL, buf->map_size, PROT_READ | PROT_WRITE,
			 MAP_SHARED, buf->fd, 0);
		if (p == MAP_FAILED) {
			perror("mmap");
			exit(-1);
		}
		for (i = 0; i< size; i++)
			if (p[i] != c) errors++;
		munmap(p, buf->map_size);
	}

	printf("%zd errors found\n", errors);
}

int str_to_location(char *s)
{
	if (!strncasecmp(s, "m", 1))
		return MALLOC;
	else if (!strncasecmp(s, "h", 1))
		return HOST;
	else if (!strncasecmp(s, "d", 1))
		return DEVICE;
	else
		return SHARED;
}

char *location_to_str(int location)
{
	if (location == MALLOC)
		return "Host Memory (malloc)";
	else if (location == HOST)
		return "Host Memory (ze)";
	else if (location == DEVICE)
		return "Device Memory";
	else
		return "Shared Memory";
}

void run_test(struct my_buf *src_buf, struct my_buf *dst_buf)
{
	size_t size;
	int i;
	double t1, t2;

	signal(SIGSEGV, segv_handler);

	if (setjmp(env))
		return;

	fill_buffer(src_buf, 'a', MAX_MSG_SIZE);
	for (size = 1; size <= MAX_MSG_SIZE; size <<= 1) {
		if (cache_cmdl && copy_method == CMDQ && !use_imm_cmdl) {
			EXIT_ON_ERROR(libze_ops.zeCommandListAppendMemoryCopy(
						gpu.cmdl, dst_buf->buf,
						src_buf->buf, size, NULL, 0,
						NULL));
			EXIT_ON_ERROR(libze_ops.zeCommandListClose(gpu.cmdl));
		}

		t1 = when();
		for (i = 0; i < iterations; i++)
			copy_buffer(src_buf, dst_buf, size);
		t2 = when();

		if (cache_cmdl && copy_method == CMDQ && !use_imm_cmdl)
			EXIT_ON_ERROR(libze_ops.zeCommandListReset(gpu.cmdl));

		printf("%8ld (x%d):%12.2lfus%12.2lfMB/s\n", size, iterations,
			t2 - t1, size * iterations / ((t2-t1)));
	}
	printf("Verifying data ......\n");
	check_buffer(dst_buf, 'a', MAX_MSG_SIZE);
}

void usage(char *prog_name)
{
	printf("Usage: %s [<options>] <src> <dst>\n", prog_name);
	printf("Options:\n");
	printf("\t-n <iterations>     number of iterations to perform copy operation for each message size\n");
	printf("\t-c <copy-method>    method used for copy operations, can be 'cmdq' (default), 'memcpy', 'mmap', and 'mmap-device'\n");
	printf("\t-C                  cache the command list, or cache mmap\n");
	printf("\t-i                  use immediate command list\n");
	printf("\t-d <device>         device to use (default: 0)\n");
	printf("\t-D <device2>        second device to use (default: the same as the first device)\n");
	printf("\t-G <ordinal>        command queue group ordinal (default: 0)\n");
	printf("\t-I <index>          engine index within the command queue group (default: 0)\n");
	printf("\t-r                  reverse the direction by creating command queue on the device with the destination buffer\n");
	printf("\t-M                  import malloc'ed buffer into L0 before the copy\n");
	printf("\t<src>               location of source buffer -- 'M':malloc, 'H':host, 'D':device, 'S':shared\n");
	printf("\t<dst>               location of destination buffer -- 'M':malloc, 'H':host, 'D':device, 'S':shared\n");
	exit(1);
}

int main(int argc, char **argv)
{
	struct my_buf src_buf = {};
	struct my_buf dst_buf = {};
	int c;

	while ((c = getopt(argc, argv, "n:c:Cid:D:G:I:rMh")) != -1)
	{
		switch (c) {
		case 'n':
			iterations = atoi(optarg);
			break;
		case 'c':
			if (!strcasecmp(optarg, "cmdq"))
				copy_method = CMDQ;
			else if (!strcasecmp(optarg, "memcpy"))
				copy_method = MEMCPY;
			else if (!strcasecmp(optarg, "mmap"))
				copy_method = MMAP, mmap_all = 1;
			else if (!strcasecmp(optarg, "mmap-device"))
				copy_method = MMAP, mmap_all = 0;
			else
				printf("Invalid copy method '%s', using default (cmdq).\n",
					optarg);
			break;
		case 'C':
			cache_cmdl = cache_mmap = 1;
			break;
		case 'i':
			use_imm_cmdl = 1;
			break;
		case 'd':
			card_num = atoi(optarg);
			break;
		case 'D':
			card_num2 = atoi(optarg);
			break;
		case 'G':
			ordinal = atoi(optarg);
			break;
		case 'I':
			engine_index = atoi(optarg);
			break;
		case 'r':
			reverse = 1;
			break;
		case 'M':
			import = 1;
			break;
		default:
			usage(argv[0]);
			break;
		}
	}

	if (argc < optind + 2)
		usage(argv[0]);

	src_buf.where = str_to_location(argv[optind]);
	dst_buf.where = str_to_location(argv[optind + 1]);

	if (card_num2 == 0xFF || src_buf.where == HOST || dst_buf.where == HOST)
		card_num2 = card_num;

	init_gpu();

	alloc_buffer(&src_buf, MAX_MSG_SIZE, gpu.dev);
	alloc_buffer(&dst_buf, MAX_MSG_SIZE, gpu.dev2);

	printf("Copy from %s to %s, ",
		location_to_str(src_buf.where), location_to_str(dst_buf.where));

	if (copy_method == MEMCPY)
		printf("using memcpy\n");
	else if (copy_method == MMAP)
		printf("using mmap (%s) on %s\n",
			cache_mmap ? "cached" : "non-cached",
			mmap_all ? "all memory except malloc" : "device memory");
	else
		printf("using %s command list (%s)\n",
			use_imm_cmdl ? "immediate" : "regular",
			cache_cmdl ? "cached" : "non-cached");

	printf("Import external pointers: %s\n", import ? "yes" : "no");

	run_test(&src_buf, &dst_buf);

	free_buffer(&src_buf);
	free_buffer(&dst_buf);

	finalize_gpu();
	return 0;
}

