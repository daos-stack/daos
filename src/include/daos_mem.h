/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * DAOS memory attributes and registered memory regions. They describe the buffers of an I/O
 * (host or device memory, e.g. a GPU) for the object, array and DFS APIs that take memory
 * attributes (the _ma calls).
 */

#ifndef __DAOS_MEM_API_H__
#define __DAOS_MEM_API_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <daos_types.h>
#include <daos_event.h>

/** Memory type of the buffers of an I/O, see daos_mem_attr_t */
typedef enum {
	DAOS_MEM_TYPE_HOST = D_MEM_TYPE_HOST, /**< System/host memory (default) */
	DAOS_MEM_TYPE_CUDA = D_MEM_TYPE_CUDA, /**< NVIDIA CUDA device memory (cudaMalloc()) */
	DAOS_MEM_TYPE_ROCM = D_MEM_TYPE_ROCM, /**< AMD ROCm device memory */
	DAOS_MEM_TYPE_ZE   = D_MEM_TYPE_ZE,   /**< Intel Level Zero device memory */
} daos_mem_type_t;

/** Opaque handle of a memory region registered with daos_mem_register(). */
typedef struct daos_mem_handle *daos_mem_handle_t;

/**
 * Side-channel memory attributes for GPU direct I/O.
 * Used alongside d_sg_list_t to describe memory type without changing
 * the d_iov_t wire format. Only consumed locally for bulk handle creation.
 */
typedef struct {
	/** memory type of all buffers in the associated sgl */
	daos_mem_type_t   ma_mem_type;
	/**
	 * Device ordinal as seen by the calling process, e.g. the CUDA device index after
	 * CUDA_VISIBLE_DEVICES is applied (what cudaGetDevice() returns). The transport registers
	 * the memory on that device (libfabric FI_HMEM_CUDA). 0 for host memory.
	 */
	uint64_t          ma_device_id;
	/**
	 * Region registered with daos_mem_register() (optional). Every iov of the associated sgl
	 * must lie inside that region, and the memory type and device must match. NULL for the
	 * regular per-I/O registration.
	 *
	 * Registration cost: without ma_handle, every I/O creates a new bulk handle, which
	 * registers (pins and maps for the NIC) the GPU buffer and deregisters it when the I/O
	 * completes. The cost is negligible for large transfers but can dominate latency for
	 * small, frequent I/O such as KV-cache blocks. Callers that reuse buffers should register
	 * them once with daos_mem_register().
	 */
	daos_mem_handle_t ma_handle;
	/**
	 * Reserved for user-supplied data checksums. Must be empty (iov_buf == NULL and
	 * iov_len == 0), otherwise the I/O fails with -DER_INVAL.
	 */
	d_iov_t           ma_csums;
} daos_mem_attr_t;

/**
 * Register a memory region, typically a large GPU buffer pool, so that later I/O on blocks inside
 * it does not pay a registration for every I/O.
 *
 * The handle can be used by I/O on any event queue and by blocking I/O. The transport MR cache
 * belongs to one network domain, and each event queue has its own, so the region is registered
 * on a domain by daos_mem_bind(), or else by the first I/O there (which pays for it), and kept
 * in that domain's cache until daos_mem_deregister() or the destruction of the event queue.
 * Each domain in use pins the region once. Pass the handle in daos_mem_attr_t::ma_handle of
 * daos_obj_fetch_ma() / daos_obj_update_ma(); every iov of that sgl must lie inside the region
 * and the memory type and device must match. The sgl need not match the region.
 *
 * If the region cannot be registered on a context (e.g. too large for the GPU BAR or the NIC),
 * the failure is logged once and remembered for that context, and I/O there registers its own
 * buffers as if no handle was passed. Likewise if the cache later evicts the entry.
 *
 * A region registered on a context uses one slot of the client bulk quota of that context
 * (D_QUOTA_BULKS, 64 by default) until daos_mem_deregister() or the destruction of the event
 * queue. A device memory region is registered even over the quota. A host memory region is not
 * registered while the context is over its quota: I/O registers its own buffers, and a later
 * I/O tries again.
 *
 * The region is registered by the thread that calls daos_mem_bind(), or the first I/O; for the
 * requirements on that thread (CUDA context) see daos_obj_update_ma().
 *
 * Fails with -DER_UNINIT before daos_init(). Fails with -DER_NOTSUPPORTED for device memory that
 * cannot be used at all: CaRT runs without memory device support (D_MEM_DEVICE unset) or Mercury
 * has no bulk memory attributes. Fails with -DER_NOSYS if only the registered region is
 * unavailable, because the client MR cache is disabled for the memory type of the region (see
 * CRT_MRC_ENABLE, off by default for ofi+verbs;ofi_rxm clients, or turned off through
 * FI_MR_CACHE_MAX_COUNT=0, FI_MR_CACHE_MAX_SIZE=0, UCX_RCACHE_ENABLE=n,
 * FI_MR_CACHE_MONITOR=disabled for host memory, or FI_MR_CUDA_CACHE_MONITOR_ENABLED=0 and its
 * ROCR and ZE equivalents for device memory); I/O without ma_handle still works then and
 * registers per I/O.
 *
 * An event queue must not be destroyed, even with DAOS_EQ_DESTROY_FORCE, while I/O on it is
 * running.
 *
 * \param[in]	buf	Start of the region.
 * \param[in]	len	Length of the region in bytes.
 * \param[in]	attr	Memory attributes of the region. NULL for host memory. ma_handle
 *			must be empty.
 * \param[out]	mh	Returned handle.
 *
 * \return		0 on success, negative DER_* on error.
 */
int
daos_mem_register(void *buf, size_t len, const daos_mem_attr_t *attr, daos_mem_handle_t *mh);

/**
 * Bind a registered region to a network domain: register it there now, instead of on the first
 * I/O of the event queue that owns the domain (see daos_eq_net_domain()). This moves the cost of
 * registering a large region to a time of the caller's choice, and reports a region that cannot
 * be registered (e.g. too large for the GPU BAR or the NIC), which the first I/O only logs.
 *
 * The call is optional. A region can be bound to several domains, and I/O through the handle on
 * an event queue whose domain it is not bound to registers the region there, as without this
 * call. There is no unbind: the binding ends with daos_mem_deregister() or the destruction of the
 * event queue. The event queue must not be destroyed during the call, and \a domain must not be
 * used after it: the call cannot detect a domain whose event queue is gone.
 *
 * \param[in]	mh	Handle from daos_mem_register().
 * \param[in]	domain	Network domain from daos_eq_net_domain().
 *
 * \return		0 if the region is registered on that domain. -DER_INVAL if \a mh or
 *			\a domain is NULL. -DER_AGAIN for a host memory region if the domain
 *			is over its bulk quota (I/O in flight, or too many regions held, see
 *			daos_mem_register()); the call can be repeated, and I/O retries by itself.
 *			Any other negative DER_* is the reason why the region cannot be registered
 *			on that domain, for example -DER_NOMEM for a region too large for the GPU
 *			BAR or the NIC: it is remembered, later calls return it again, and I/O
 *			there registers its own buffers.
 */
int
daos_mem_bind(daos_mem_handle_t mh, daos_net_domain_t domain);

/**
 * Deregister a region registered by daos_mem_register().
 *
 * The caller must not submit I/O with the handle or call daos_mem_bind() on it once
 * deregistration starts, nor call it while another thread may still do so: the handle is freed
 * on success.
 *
 * \param[in]	mh	Handle to deregister.
 *
 * \return		0 on success, -DER_BUSY if I/O using the handle is in flight, other negative
 *			DER_* on error. The handle stays valid after an error.
 */
int
daos_mem_deregister(daos_mem_handle_t mh);

#if defined(__cplusplus)
}
#endif /* __cplusplus */
#endif /* __DAOS_MEM_API_H__ */
