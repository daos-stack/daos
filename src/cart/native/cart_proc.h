/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef CRT_NATIVE_CART_PROC_H
#define CRT_NATIVE_CART_PROC_H
#include <cart/api.h>
#include "proc.h"

/* Connection-specific bulk binding. Uses crt_native_cart_cursor(proc) to
 * encode/decode descriptors. Owns decoded wrapper allocation and FREE, and
 * must retain exports until peer acknowledgment or remote quiescence.
 * Return CaRT (-DER_*) errors, not errno. NULL bulk values are supported even
 * without this binding; non-NULL native handles require it.
 */
typedef int (*crt_native_cart_bulk_cb_t)(crt_proc_t, crt_proc_op_t, crt_bulk_t *, void *);

/* Run an existing CaRT crf_proc_in/out callback with a stack-owned native proc.
 * Recognition uses exact pointer equality against a thread-local scope stack;
 * Mercury handles are never dereferenced or guessed to be native cursors.
 * Nested scopes and nested Mercury callbacks are supported. The proc must not
 * escape this synchronous call. DECODE views borrow buffer; retain its RX lease
 * before calling this for asynchronously dispatched requests. FREE applies to
 * objects decoded by this backend, never arbitrary application-owned objects.
 * Arrays allocate metadata; strings/iovs remain borrowed.
 */
int
crt_native_cart_run(crt_proc_cb_t, void *data, crt_proc_op_t, void *buffer, size_t,
		    crt_native_cart_bulk_cb_t, void *bulk_arg, size_t *used);
struct crt_native_proc *crt_native_cart_cursor(crt_proc_t);
struct crt_rpc_priv;
/* Header and registered body schema for a preallocated CaRT RPC. Caller sets
 * encode headers (HLC, flags, ranks and deadlines); the transport/dispatch
 * bridge validates decoded HLC, peer identity and routing before dispatch.
 */
int
crt_native_cart_payload(struct crt_rpc_priv *, bool reply, crt_proc_op_t, void *buffer, size_t,
			crt_native_cart_bulk_cb_t, void *, size_t *used);

/* Internal Mercury fallbacks for public crt_proc_* dispatch. */
#define CRT_HG_SCALAR(type) int crt_hg_proc_##type(crt_proc_t, crt_proc_op_t, type *)
CRT_HG_SCALAR(int8_t);
CRT_HG_SCALAR(uint8_t);
CRT_HG_SCALAR(int16_t);
CRT_HG_SCALAR(uint16_t);
CRT_HG_SCALAR(int32_t);
CRT_HG_SCALAR(uint32_t);
CRT_HG_SCALAR(int64_t);
CRT_HG_SCALAR(uint64_t);
CRT_HG_SCALAR(bool);
#undef CRT_HG_SCALAR
int
crt_hg_proc_get_op(crt_proc_t, crt_proc_op_t *);
int
crt_hg_proc_memcpy(crt_proc_t, crt_proc_op_t, void *, size_t);
int
crt_hg_proc_crt_bulk_t(crt_proc_t, crt_proc_op_t, crt_bulk_t *);
int
crt_hg_proc_d_string_t(crt_proc_t, crt_proc_op_t, d_string_t *);
int
crt_hg_proc_d_const_string_t(crt_proc_t, crt_proc_op_t, d_const_string_t *);
int
crt_hg_proc_d_rank_list_t(crt_proc_t, crt_proc_op_t, d_rank_list_t **);
int
crt_hg_proc_d_iov_t(crt_proc_t, crt_proc_op_t, d_iov_t *);
int
crt_hg_proc_d_sg_list_t(crt_proc_t, crt_proc_op_t, d_sg_list_t *);
#endif
