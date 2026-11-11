/**
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(object)

#include <daos/task.h>
#include "cli_mem_attr.h"

static bool
mem_attr_iov_in_region(const char *base, size_t region_len, const d_iov_t *iov)
{
	size_t len = iov->iov_buf_len > iov->iov_len ? iov->iov_buf_len : iov->iov_len;
	char  *buf = iov->iov_buf;

	if (len == 0)
		return true;
	if (buf < base || (size_t)(buf - base) > region_len)
		return false;
	return len <= region_len - (size_t)(buf - base);
}

static int
mem_attr_handle_valid(const daos_mem_attr_t *mem_attr, const d_sg_list_t *sgl, unsigned int idx)
{
	d_mem_dev_t mdev;
	void       *base;
	size_t      len;
	uint32_t    j;
	int         rc;

	rc = crt_mem_query(dc_mem_handle2crt(mem_attr->ma_handle), &base, &len, &mdev);
	if (rc != 0)
		return rc;

	if ((d_mem_type_t)mem_attr->ma_mem_type != mdev.md_type ||
	    mem_attr->ma_device_id != mdev.md_device_id) {
		D_ERROR("sgl %u: memory type or device differs from the registered region\n", idx);
		return -DER_INVAL;
	}
	if (sgl == NULL || sgl->sg_iovs == NULL)
		return 0;

	for (j = 0; j < sgl->sg_nr; j++) {
		if (!mem_attr_iov_in_region(base, len, &sgl->sg_iovs[j])) {
			D_ERROR("sgl %u iov %u is outside the registered region\n", idx, j);
			return -DER_INVAL;
		}
	}
	return 0;
}

int
dc_obj_mem_attrs_valid(const daos_mem_attr_t *mem_attrs, d_sg_list_t *sgls, unsigned int nr)
{
	unsigned int i;
	int          rc;

	if (mem_attrs == NULL)
		return 0;

	for (i = 0; i < nr; i++) {
		const daos_mem_attr_t *mem_attr = &mem_attrs[i];

		switch (mem_attr->ma_mem_type) {
		case DAOS_MEM_TYPE_HOST:
			if (mem_attr->ma_device_id != 0) {
				D_ERROR("invalid host memory attributes for sgl %u\n", i);
				return -DER_INVAL;
			}
			break;
		case DAOS_MEM_TYPE_CUDA:
		case DAOS_MEM_TYPE_ROCM:
		case DAOS_MEM_TYPE_ZE:
			break;
		default:
			D_ERROR("invalid memory type %d for sgl %u\n", mem_attr->ma_mem_type, i);
			return -DER_INVAL;
		}

		if (mem_attr->ma_csums.iov_buf != NULL || mem_attr->ma_csums.iov_len != 0) {
			D_ERROR("ma_csums is reserved and must be empty (sgl %u)\n", i);
			return -DER_INVAL;
		}

		if (mem_attr->ma_handle != NULL) {
			rc = mem_attr_handle_valid(mem_attr, sgls != NULL ? &sgls[i] : NULL, i);
			if (rc != 0)
				return rc;
		}
	}

	return 0;
}

uint64_t
dc_obj_mem_attrs_flags(const daos_mem_attr_t *mem_attrs, unsigned int nr, uint64_t flags)
{
	unsigned int i;

	if (mem_attrs == NULL)
		return flags;

	for (i = 0; i < nr; i++) {
		if (mem_attrs[i].ma_mem_type != DAOS_MEM_TYPE_HOST)
			return flags | DAOS_OBJ_IO_GPU_DIRECT;
	}

	return flags;
}

bool
dc_obj_mem_attrs_has_reg(const daos_mem_attr_t *mem_attrs, unsigned int nr)
{
	unsigned int i;

	if (mem_attrs == NULL)
		return false;

	for (i = 0; i < nr; i++) {
		if (mem_attrs[i].ma_handle != NULL)
			return true;
	}
	return false;
}

int
dc_obj_mem_attrs_dup(const daos_mem_attr_t *mem_attrs, unsigned int nr, daos_mem_attr_t **dup_out)
{
	daos_mem_attr_t *dup;
	unsigned int     i;

	D_ALLOC_ARRAY(dup, nr);
	if (dup == NULL)
		return -DER_NOMEM;

	for (i = 0; i < nr; i++) {
		dup[i].ma_mem_type  = mem_attrs[i].ma_mem_type;
		dup[i].ma_device_id = mem_attrs[i].ma_device_id;

		dup[i].ma_handle = mem_attrs[i].ma_handle;
		if (dup[i].ma_handle != NULL)
			crt_mem_addref(dc_mem_handle2crt(dup[i].ma_handle));
	}

	*dup_out = dup;
	return 0;
}

void
dc_obj_mem_attrs_free(daos_mem_attr_t *mem_attrs, unsigned int nr)
{
	unsigned int i;

	if (mem_attrs == NULL)
		return;

	for (i = 0; i < nr; i++) {
		if (mem_attrs[i].ma_handle != NULL)
			crt_mem_decref(dc_mem_handle2crt(mem_attrs[i].ma_handle));
	}

	D_FREE(mem_attrs);
}

int
dc_obj_mem_attr_ctx_prep(const daos_mem_attr_t *mem_attr, crt_context_t ctx)
{
	int rc;

	if (mem_attr == NULL || mem_attr->ma_handle == NULL)
		return 0;

	/*
	 * Register the region on the context of the I/O on first use. If the region cannot be
	 * registered, the I/O goes on and registers its own buffers; only device memory that
	 * cannot be used at all fails here, as the bulk of the I/O would.
	 */
	rc = crt_mem_bind(dc_mem_handle2crt(mem_attr->ma_handle), ctx);
	return rc == -DER_NOTSUPPORTED ? rc : 0;
}
