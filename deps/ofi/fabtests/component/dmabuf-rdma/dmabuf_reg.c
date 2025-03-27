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
 * dmabuf registry for peer-memory access.
 *
 * The dmabuf registry is a database of known dmabuf based allocations
 * maintained by the "dmabuf_peer_mem" kernel module. The module provides a
 * ib-peer-memory client that plugs into the RDMA stack of MOFED installation.
 * It allows buffer allocated on the device memory be registered via the
 * regular ibv_reg_mr() call.
 *
 * The "dmabuf_peer_mem" kernel module is not needed if the RDMA stack has
 * native dmabuf support and the buffer is registered with the newer
 * ibv_reg_dmabuf_mr() call.
 *
 * The code here is for explicitly accesing the dmabuf registry upon buffer
 * allocation. It is not needed for libfabric based applications when the
 * "dmabuf_peer_mem" hooking provider is enabled (by setting the environment
 * variable FI_HOOK=dmabuf_peer_mem).
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "dmabuf_reg.h"

static char *dmabuf_reg_dev_name = "/dev/" DMABUF_REG_DEV_NAME;
static int dmabuf_reg_fd = -1;

int dmabuf_reg_open(void)
{
	int fd;

	fd = open(dmabuf_reg_dev_name, 0);
	if (fd < 0) {
		perror(dmabuf_reg_dev_name);
		return fd;
	}

	dmabuf_reg_fd = fd;
	return 0;
}

void dmabuf_reg_close(void)
{
	if (dmabuf_reg_fd >= 0)
		close(dmabuf_reg_fd);
}

int dmabuf_reg_add(uint64_t base, uint64_t size, int fd)
{
	struct dmabuf_reg_param args = {
		.op = DMABUF_REG_ADD,
		.base = base,
		.size = size,
		.fd = fd,
	};
	int err;

	err = ioctl(dmabuf_reg_fd, DMABUF_REG_IOCTL, &args);
	if (err)
		perror(__func__);

	return err;
}

void dmabuf_reg_remove(uint64_t addr)
{
	struct dmabuf_reg_param args = {
		.op = DMABUF_REG_REMOVE_ADDR,
		.base = addr,
	};
	int err;

	err = ioctl(dmabuf_reg_fd, DMABUF_REG_IOCTL, &args);
	if (err)
		perror(__func__);
}

