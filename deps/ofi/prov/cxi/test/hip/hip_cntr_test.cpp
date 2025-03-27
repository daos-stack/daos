/*
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * Copyright (c) 2021 Hewlett Packard Enterprise Development LP
 */

#include "hip/hip_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_cxi_ext.h>

/* Example compile instructions. */
// hipcc --amdgpu-target=gfx908 -I<path_to>/libfabric/install/include -L/opt/rocm/lib64/ -L/opt/rocm/lib -L<path_to>/libfabric/install/lib -lfabric -g -c hip_cntr_test.cpp
// hipcc --amdgpu-target=gfx908 -I<path_to>/libfabric/install/include -L/opt/rocm/lib64/ -L/opt/rocm/lib -L<path_to>/libfabric/install/lib -lfabric -g hip_cntr_test.o -o hip_cntr_test

#define GPU_WB_SIZE 8U

static struct fi_info *hints;
static struct fi_info *info;
static struct fid_fabric *fabric;
static struct fid_domain *domain;
static struct fid_cntr *cntr;
static struct fi_cxi_cntr_ops *cntr_ops;
static void *gpu_wb;

void resource_init(void)
{
	int ret;

	ret = hipMalloc(&gpu_wb, GPU_WB_SIZE);
	assert(ret == hipSuccess);

	hints  = fi_allocinfo();
	assert(hints != NULL);

	/* Always select CXI provider */
	hints->domain_attr->mr_mode = FI_MR_ENDPOINT;
	hints->fabric_attr->prov_name = strdup("cxi");
	assert(hints->fabric_attr->prov_name != NULL);

	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION), NULL,
			 NULL, 0, hints, &info);
	assert(ret == FI_SUCCESS);

	ret = fi_fabric(info->fabric_attr, &fabric, NULL);
	assert(ret == FI_SUCCESS);

	ret = fi_domain(fabric, info, &domain, NULL);
	assert(ret == FI_SUCCESS);

	ret = fi_cntr_open(domain, NULL, &cntr, NULL);
	assert(ret == FI_SUCCESS);

	ret = fi_open_ops(&cntr->fid, FI_CXI_COUNTER_OPS, 0, (void **)&cntr_ops,
			  NULL);
	assert(ret == FI_SUCCESS);

	ret = cntr_ops->set_wb_buffer(&cntr->fid, gpu_wb, GPU_WB_SIZE);
	assert(ret == FI_SUCCESS);
}

void resource_free(void)
{
	fi_close(&cntr->fid);
	fi_close(&domain->fid);
	fi_close(&fabric->fid);
	fi_freeinfo(info);
	fi_freeinfo(hints);
	hipFree(gpu_wb);
}

int main(int argc, char *argv[])
{
	int ret;

	resource_init();

	ret = fi_cntr_adderr(cntr, 5);
	assert(ret == FI_SUCCESS);

	while (fi_cntr_readerr(cntr) != 5);

	ret = fi_cntr_add(cntr, 123);
	assert(ret == FI_SUCCESS);

	while (fi_cntr_read(cntr) != 123);
	while (fi_cntr_readerr(cntr) != 5);

	resource_free();

	return 0;
}
