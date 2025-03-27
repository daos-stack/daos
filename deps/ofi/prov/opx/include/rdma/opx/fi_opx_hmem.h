/*
 * Copyright (C) 2023-2024 by Cornelis Networks.
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
#ifndef _FI_PROV_OPX_HMEM_H_
#define _FI_PROV_OPX_HMEM_H_

#include <assert.h>
#include <rdma/hfi/hfi1_user.h>
#include "rdma/opx/fi_opx_compiler.h"
#include "rdma/opx/fi_opx_rma_ops.h"
#include "rdma/opx/opx_tracer.h"
#include "ofi_hmem.h"

#define OPX_HMEM_NO_HANDLE (0)
#define OPX_HMEM_DEV_REG_THRESHOLD_NOT_SET (-1L)

#ifdef OPX_HMEM
#define OPX_HMEM_DEV_REG_SEND_THRESHOLD	(opx_ep->domain->hmem_domain->devreg_copy_from_threshold)
#define OPX_HMEM_DEV_REG_RECV_THRESHOLD	(opx_ep->domain->hmem_domain->devreg_copy_to_threshold)
#else
#define OPX_HMEM_DEV_REG_SEND_THRESHOLD	(OPX_HMEM_DEV_REG_THRESHOLD_NOT_SET)
#define OPX_HMEM_DEV_REG_RECV_THRESHOLD	(OPX_HMEM_DEV_REG_THRESHOLD_NOT_SET)
#endif

struct fi_opx_hmem_info {
	uint64_t			device;
	uint64_t			hmem_dev_reg_handle;
	enum fi_hmem_iface		iface;
	uint32_t			unused;
} __attribute__((__packed__)) __attribute__((aligned(8)));

OPX_COMPILE_TIME_ASSERT((sizeof(struct fi_opx_hmem_info) & 0x7) == 0,
			"sizeof(fi_opx_hmem_info) should be a multiple of 8");

__OPX_FORCE_INLINE__
enum fi_hmem_iface fi_opx_hmem_get_iface(const void *ptr,
					 const struct fi_opx_mr *desc,
					 uint64_t *device)
{
#ifdef OPX_HMEM
	if (desc) {
		switch (desc->attr.iface) {
			case FI_HMEM_CUDA:
				*device = desc->attr.device.cuda;
				break;
			case FI_HMEM_ZE:
				*device = desc->attr.device.ze;
				break;
			default:
				*device = 0ul;
		}
		return desc->attr.iface;
	}

	#if HAVE_CUDA
		unsigned mem_type;
		unsigned is_managed;
		unsigned device_ordinal;

		/* Each pointer in 'data' needs to have the same array index
		   as the corresponding attribute in 'cuda_attributes' */
		void *data[] = {&mem_type, &is_managed, &device_ordinal};

		enum CUpointer_attribute_enum cuda_attributes[] = {
			CU_POINTER_ATTRIBUTE_MEMORY_TYPE,
			CU_POINTER_ATTRIBUTE_IS_MANAGED,
			CU_POINTER_ATTRIBUTE_DEVICE_ORDINAL
		};

		CUresult cuda_rc = cuPointerGetAttributes(ARRAY_SIZE(cuda_attributes),
							  cuda_attributes, data,
							  (CUdeviceptr) ptr);

		if (cuda_rc == CUDA_SUCCESS) {

			if (mem_type == CU_MEMORYTYPE_DEVICE && !is_managed) {
				*device = device_ordinal;
				return FI_HMEM_CUDA;
			}
		} else if (cuda_rc != CUDA_ERROR_INVALID_CONTEXT) {
			FI_DBG_TRACE(fi_opx_global.prov, FI_LOG_EP_DATA,
				"Bad return code %hu from cuPointerGetAttributes()",
				cuda_rc);
		}
	#else
		enum fi_hmem_iface iface = ofi_get_hmem_iface(ptr, device, NULL);
		return iface;
	#endif
#endif

	*device = 0ul;
	return FI_HMEM_SYSTEM;
}

__OPX_FORCE_INLINE__
int opx_copy_to_hmem(enum fi_hmem_iface iface, uint64_t device, uint64_t hmem_handle,
		     void *dest, const void *src, size_t len, int64_t threshold)
{
	// These functions should never be called for regular host memory.
	// Calling this function directly should only ever be done in code
	// paths where we know iface != FI_HMEM_SYSTEM. Otherwise, the
	// OPX_HMEM_COPY_* macros should be used
	assert(iface != FI_HMEM_SYSTEM);

	int ret;

	assert((hmem_handle == OPX_HMEM_NO_HANDLE && threshold == OPX_HMEM_DEV_REG_THRESHOLD_NOT_SET) ||
		(hmem_handle != OPX_HMEM_NO_HANDLE && threshold != OPX_HMEM_DEV_REG_THRESHOLD_NOT_SET));

	OPX_TRACER_TRACE(OPX_TRACER_BEGIN, "COPY-TO-HMEM");
	switch (iface) {
#if HAVE_CUDA
		case FI_HMEM_CUDA:
			if ((hmem_handle != 0) && (len <= threshold)) {
				OPX_TRACER_TRACE(OPX_TRACER_BEGIN, "GDRCOPY-TO-DEV");
				cuda_gdrcopy_to_dev(hmem_handle, dest, src, len);
				OPX_TRACER_TRACE(OPX_TRACER_END_SUCCESS, "GDRCOPY-TO-DEV");
				ret = 0;
			} else {
				OPX_TRACER_TRACE(OPX_TRACER_BEGIN, "CUDAMEMCPY-TO-HMEM");
				ret = (int) cudaMemcpy(dest, src, len, cudaMemcpyHostToDevice);
				OPX_TRACER_TRACE(OPX_TRACER_END_SUCCESS, "CUDAMEMCPY-TO-HMEM");
			}
			break;
#endif

#if HAVE_ROCR
		case FI_HMEM_ROCR:
			if ((hmem_handle != 0) && (len <= threshold)) {
				/* Perform a device registered copy */
				OPX_TRACER_TRACE(OPX_TRACER_BEGIN, "AMD-DEV-REG-COPY-TO-DEV");
				ret = rocr_dev_reg_copy_to_hmem(hmem_handle, dest, src, len);
				OPX_TRACER_TRACE(OPX_TRACER_END_SUCCESS, "AMD-DEV-REG-COPY-TO-DEV");
			} else {
				/* Perform standard rocr_memcopy*/
				OPX_TRACER_TRACE(OPX_TRACER_BEGIN, "AMD-ROCR-MEMCOPY-TO-HMEM");
				ret = rocr_copy_to_dev(device, dest, src, len);
				OPX_TRACER_TRACE(OPX_TRACER_END_SUCCESS, "AMD-ROCR-MEMCOPY-TO-HMEM");
			}
			break;
#endif

		default:
			OPX_TRACER_TRACE(OPX_TRACER_BEGIN, "OFI-COPY-TO-HMEM");
			ret = ofi_copy_to_hmem(iface, device, dest, src, len);
			OPX_TRACER_TRACE(OPX_TRACER_END_SUCCESS, "OFI-COPY-TO-HMEM");
			break;
	}

	OPX_TRACER_TRACE(OPX_TRACER_END_SUCCESS, "COPY-TO-HMEM");
	return ret;
}

__OPX_FORCE_INLINE__
int opx_copy_from_hmem(enum fi_hmem_iface iface, uint64_t device, uint64_t hmem_handle,
		       void *dest, const void *src, size_t len, int64_t threshold)
{
	// These functions should never be called for regular host memory.
	// Calling this function directly should only ever be done in code
	// paths where we know iface != FI_HMEM_SYSTEM. Otherwise, the
	// OPX_HMEM_COPY_* macros should be used
	assert(iface != FI_HMEM_SYSTEM);

	int ret;

	assert((hmem_handle == OPX_HMEM_NO_HANDLE && threshold == OPX_HMEM_DEV_REG_THRESHOLD_NOT_SET) ||
		(hmem_handle != OPX_HMEM_NO_HANDLE && threshold != OPX_HMEM_DEV_REG_THRESHOLD_NOT_SET));

	OPX_TRACER_TRACE(OPX_TRACER_BEGIN, "COPY-FROM-HMEM");
	switch (iface) {
#if HAVE_CUDA
		case FI_HMEM_CUDA:
			if ((hmem_handle != 0) && (len <= threshold)) {
				OPX_TRACER_TRACE(OPX_TRACER_BEGIN, "GDRCOPY-FROM-DEV");
				cuda_gdrcopy_from_dev(hmem_handle, dest, src, len);
				OPX_TRACER_TRACE(OPX_TRACER_END_SUCCESS, "GDRCOPY-FROM-DEV");
				ret = 0;
			} else {
				OPX_TRACER_TRACE(OPX_TRACER_BEGIN, "CUDAMEMCPY-FROM-HMEM");
				ret = (int) cudaMemcpy(dest, src, len, cudaMemcpyDeviceToHost);
				OPX_TRACER_TRACE(OPX_TRACER_END_SUCCESS, "CUDAMEMCPY-FROM-HMEM");
			}
			break;
#endif

#if HAVE_ROCR
		case FI_HMEM_ROCR:
			if ((hmem_handle != 0) && (len <= threshold)) {
				/* Perform a device registered copy */
				OPX_TRACER_TRACE(OPX_TRACER_BEGIN, "AMD-DEV-REG-COPY-FROM-DEV");
				ret = rocr_dev_reg_copy_from_hmem(hmem_handle, dest, src, len);
				OPX_TRACER_TRACE(OPX_TRACER_END_SUCCESS, "AMD-DEV-REG-COPY-FROM-DEV");
			} else {
				/* Perform standard rocr_memcopy*/
				OPX_TRACER_TRACE(OPX_TRACER_BEGIN, "AMD-ROCR-MEMCOPY-FROM-HMEM");
				ret = rocr_copy_to_dev(device, dest, src, len);
				OPX_TRACER_TRACE(OPX_TRACER_END_SUCCESS, "AMD-ROCR-MEMCOPY-FROM-HMEM");
			}
			break;
#endif
	
		default:
			OPX_TRACER_TRACE(OPX_TRACER_BEGIN, "OFI-COPY-FROM-HMEM");
			ret = ofi_copy_from_hmem(iface, device, dest, src, len);
			OPX_TRACER_TRACE(OPX_TRACER_END_SUCCESS, "OFI-COPY-FROM-HMEM");
			break;
	}

	OPX_TRACER_TRACE(OPX_TRACER_END_SUCCESS, "COPY-FROM-HMEM");
	return ret;
}

__OPX_FORCE_INLINE__
unsigned fi_opx_hmem_iov_init(const void *buf,
				const size_t len,
				const void *desc,
				struct fi_opx_hmem_iov *iov)
{
	iov->buf = (uintptr_t) buf;
	iov->len = len;
#ifdef OPX_HMEM
	uint64_t hmem_device;
	enum fi_hmem_iface hmem_iface = fi_opx_hmem_get_iface(buf, desc, &hmem_device);
	iov->iface = hmem_iface;
	iov->device = hmem_device;
	return (hmem_iface != FI_HMEM_SYSTEM);
#else
	iov->iface = FI_HMEM_SYSTEM;
	iov->device = 0ul;
	return 0;
#endif
}

static const unsigned OPX_HMEM_KERN_MEM_TYPE[4] = {
	#ifdef OPX_HMEM
		HFI1_MEMINFO_TYPE_SYSTEM,
		HFI1_MEMINFO_TYPE_NVIDIA,
		2, /* HFI1_MEMINFO_TYPE_AMD */
		1 /* HFI1_MEMINFO_TYPE_DMABUF */
	#endif
};

static const unsigned OPX_HMEM_OFI_MEM_TYPE[4] = {
	#ifdef OPX_HMEM
		FI_HMEM_SYSTEM,		/* HFI1_MEMINFO_TYPE_SYSTEM */
		FI_HMEM_ZE,		/* HFI1_MEMINFO_TYPE_DMABUF */
		FI_HMEM_ROCR,		/* HFI1_MEMINFO_TYPE_AMD    */
		FI_HMEM_CUDA		/* HFI1_MEMINFO_TYPE_NVIDIA */
	#endif
};

#ifdef OPX_HMEM
#define OPX_HMEM_COPY_FROM(dst, src, len, handle, threshold, src_iface, src_device)	\
	do {										\
		if (src_iface == FI_HMEM_SYSTEM) {					\
			memcpy(dst, src, len);						\
		} else {								\
			opx_copy_from_hmem(src_iface, src_device, handle, dst,		\
					src, len, threshold);				\
		}									\
	} while (0)

#define OPX_HMEM_COPY_TO(dst, src, len, handle, threshold, dst_iface, dst_device)	\
	do {										\
		if (dst_iface == FI_HMEM_SYSTEM) {					\
			memcpy(dst, src, len);						\
		} else {								\
			opx_copy_to_hmem(dst_iface, dst_device, handle, dst,		\
					src, len, threshold);				\
		}									\
	} while (0)

#define OPX_HMEM_ATOMIC_DISPATCH(src, dst, len, dt, op, dst_iface, dst_device)		\
	do {										\
		if (dst_iface == FI_HMEM_SYSTEM) {					\
			fi_opx_rx_atomic_dispatch(src, dst, len, dt, op);		\
		} else {								\
			uint8_t hmem_buf[FI_OPX_HFI1_PACKET_MTU];			\
			opx_copy_from_hmem(dst_iface, dst_device, OPX_HMEM_NO_HANDLE,	\
					hmem_buf, dst, len,				\
					OPX_HMEM_DEV_REG_THRESHOLD_NOT_SET);		\
			fi_opx_rx_atomic_dispatch(src, hmem_buf, len, dt, op);		\
			opx_copy_to_hmem(dst_iface, dst_device, OPX_HMEM_NO_HANDLE,	\
					dst, hmem_buf, len,				\
					OPX_HMEM_DEV_REG_THRESHOLD_NOT_SET);		\
		}									\
	} while (0)

#else

#define OPX_HMEM_COPY_FROM(dst, src, len, handle, threshold, src_iface, src_device)	\
	do {										\
		memcpy(dst, src, len);							\
		(void)src_iface;							\
	} while (0)

#define OPX_HMEM_COPY_TO(dst, src, len, handle, threshold, dst_iface, dst_device)	\
	do {										\
		memcpy(dst, src, len);							\
		(void)dst_iface;							\
	} while (0)

#define OPX_HMEM_ATOMIC_DISPATCH(src, dst, len, dt, op, dst_iface, dst_device)		\
	do {										\
		fi_opx_rx_atomic_dispatch(src, dst, len, dt, op);			\
	} while (0)

#endif // OPX_HMEM

#endif
