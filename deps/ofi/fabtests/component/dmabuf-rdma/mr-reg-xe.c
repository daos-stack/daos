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
 *	mr-reg-xe.c
 *
 *	This is a simple test that checks if memory registration is working
 *	correctly. Kernel and user space RDMA/dma-buf support is required
 *	(kernel 5.12 and later, rdma-core v34 nad later, or MOFED 5.5 and
 *	later).
 *
 *	Register memory allocted with malloc():
 *
 *	    ./xe_mr_reg -m malloc
 *
 *	Register memory allocated with zeMemAllocHost():
 *
 *	    ./xe_mr_reg -m host
 *
 *	Register memory allocated with zeMemAllocDevice() on device 0
 *
 *	    ./xe_mr_reg -m device -d 0
 *
 *	For more options:
 *
 *	    ./xe_mr_reg -h
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <infiniband/verbs.h>
#include "util.h"
#include "xe.h"

static struct ibv_device	**dev_list;
static struct ibv_device	*dev;
static struct ibv_context	*context;
static struct ibv_pd		*pd;
static struct ibv_mr		*mr;

static void	*buf;
static int	buf_fd;
static size_t	buf_size = 65536;
static int	buf_location = MALLOC;

static void init_buf(void)
{
	int page_size = sysconf(_SC_PAGESIZE);

	buf = xe_alloc_buf(page_size, buf_size, buf_location, 0, NULL);
	if (!buf) {
		fprintf(stderr, "Couldn't allocate work buf.\n");
		exit(-1);
	}
}

static void free_buf(void)
{
	xe_free_buf(buf, buf_location);
}

static void free_ib(void)
{
	ibv_dealloc_pd(pd);
	ibv_close_device(context);
	ibv_free_device_list(dev_list);
}

static int init_ib(void)
{
	dev_list = ibv_get_device_list(NULL);
	if (!dev_list) {
		perror("Failed to get IB devices list");
		return -ENODEV;
	}

	dev = *dev_list;
	if (!dev) {
		fprintf(stderr, "No IB devices found\n");
		return -ENODEV;
	}

	printf("Using IB device %s\n", ibv_get_device_name(dev));
	CHECK_NULL((context = ibv_open_device(dev)));
	CHECK_NULL((pd = ibv_alloc_pd(context)));
	return 0;

err_out:
	return -1;
}

static int reg_mr(void)
{
	int mr_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
			      IBV_ACCESS_REMOTE_WRITE;

	if (use_dmabuf_reg || buf_location == MALLOC) {
		printf("Calling ibv_reg_mr(buf=%p, size=%zd)\n", buf, buf_size);
		CHECK_NULL((mr = ibv_reg_mr(pd, buf, buf_size, mr_access_flags)));
	} else {
		buf_fd = xe_get_buf_fd(buf);
		printf("Calling ibv_reg_dmabuf_mr(buf=%p, size=%zd, fd=%d)\n",
			buf, buf_size, buf_fd);
		CHECK_NULL((mr = ibv_reg_dmabuf_mr(pd, 0, buf_size,
						   (uint64_t)buf, buf_fd,
						   mr_access_flags)));
	}

	printf("%s: mr %p\n", __func__, mr);
	return 0;

err_out:
	return -1;
}

static void dereg_mr(void)
{
	if (mr)
		ibv_dereg_mr(mr);
}

static void usage(char *prog)
{
	printf("Usage: %s [options]\n", prog);
	printf("Options:\n");
	printf("\t-m <location>    Where to allocate the buffer, can be 'malloc', 'host','device' or 'shared', default: malloc\n");
	printf("\t-d <card_num>    Use the GPU device specified by <card_num>, default: 0\n");
	printf("\t-S <buf_size>    Set the size of the buffer to allocate, default: 65536\n");
	printf("\t-R               Use dmabuf_reg (plug-in for MOFED peer-mem)\n");
	printf("\t-h               Print this message\n");
}

int main(int argc, char *argv[])
{
	char *gpu_dev_nums = NULL;
	int c;

	while ((c = getopt(argc, argv, "d:m:RS:h")) != -1) {
		switch (c) {
		case 'd':
			gpu_dev_nums = strdup(optarg);
			break;
		case 'm':
			if (strcasecmp(optarg, "malloc") == 0)
				buf_location = MALLOC ;
			else if (strcasecmp(optarg, "host") == 0)
				buf_location = HOST;
			else if (strcasecmp(optarg, "device") == 0)
				buf_location = DEVICE;
			else if (strcasecmp(optarg, "shared") == 0)
				buf_location = SHARED;
			break;
		case 'R':
			use_dmabuf_reg = 1;
			break;
		case 'S':
			buf_size = atol(optarg);
			break;
		default:
			usage(argv[0]);
			exit(-1);
			break;
		}
	}

	if (use_dmabuf_reg)
		dmabuf_reg_open();

	if (buf_location != MALLOC)
		xe_init(gpu_dev_nums, 0);

	init_buf();
	init_ib();
	reg_mr();

	dereg_mr();
	free_ib();
	free_buf();

	if (use_dmabuf_reg)
		dmabuf_reg_close();

	return 0;
}

