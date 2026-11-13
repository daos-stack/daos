/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. Registered memory regions, see crt_mem_register().
 */

#ifndef __CRT_MEM_H__
#define __CRT_MEM_H__

#include <stdatomic.h>
#include <pthread.h>
#include <gurt/list.h>
#include <cart/api.h>

/** Registration of a region on one context. */
struct crt_mem_ctx_bulk {
	crt_context_t cmcb_ctx;
	/** bulk handle over the whole region, held to keep it in the transport MR cache */
	crt_bulk_t    cmcb_bulk;
	/** 0, or why the region could not be registered on this context (cmcb_bulk is NULL) */
	int           cmcb_rc;
};

/**
 * A memory region registered by crt_mem_register(). The transport MR cache is per context, so
 * the region is registered on each context by crt_mem_bind().
 */
struct crt_mem_handle {
	/** link on the global list of handles */
	d_list_t                 cmh_link;
	/** protects cmh_ctx_bulks and cmh_ctx_nr */
	pthread_rwlock_t         cmh_lock;
	struct crt_mem_ctx_bulk *cmh_ctx_bulks;
	uint32_t                 cmh_ctx_nr;
	void                    *cmh_buf;
	size_t                   cmh_len;
	d_mem_dev_t              cmh_dev;
	/** 1 for the registration itself, plus the references of crt_mem_addref() */
	_Atomic uint32_t         cmh_refs;
};

/** Free the registrations of all regions on \a ctx, before the context is destroyed. */
void
crt_mem_ctx_fini(crt_context_t ctx);

#endif /* __CRT_MEM_H__ */
