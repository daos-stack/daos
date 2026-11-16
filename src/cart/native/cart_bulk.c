/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "cart_bulk.h"
#include <errno.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stddef.h>

struct registration {
	crt_bulk_t              original;
	struct crt_native_bulk *native;
	unsigned                count;
	bool                    reclaiming;
};
struct bulk_operation {
	struct crt_native_bulk_transfer transfer;
	struct crt_bulk_desc            descriptor;
	crt_bulk_cb_t                   verify, complete;
	void                           *arg;
	unsigned                        next;
	unsigned                        incoming_next;
	bool                            initialized;
	int                             setup_error;
	int                             final_result;
	atomic_bool                     active;
	atomic_bool                     cancel;
};
static pthread_mutex_t              registry_lock = PTHREAD_MUTEX_INITIALIZER;
static struct crt_native_cart_bulk *registry;
struct crt_native_cart_bulk {
	const struct crt_native_ops             *ops;
	struct crt_native_ep                    *ep;
	struct crt_native_bulk_session          *session;
	struct registration                     *cache;
	unsigned                                 capacity, victim;
	atomic_bool                              closing;
	_Atomic(struct crt_native_cart_remote *) released;
	struct crt_native_cart_remote           *released_pending;
	atomic_uint                              remote_handles;
	struct bulk_operation                   *operations;
	unsigned                                 free_operation, pending_head, pending_tail;
	atomic_uint                              active_operations, incoming;
	unsigned                                 incoming_pending;
	struct crt_native_cart_bulk             *registry_next;
	bool                                     progressing;
	bool                                     defer_callbacks, dispatching;
	unsigned                                 completed_head, completed_tail;
	atomic_int                               error;
};

static int
result(int rc)
{
	switch (rc) {
	case 0:
		return 0;
	case EAGAIN:
	case EBUSY:
		return -DER_AGAIN;
	case ENOMEM:
		return -DER_NOMEM;
	case EMSGSIZE:
		return -DER_TRUNC;
	case EPROTO:
		return -DER_PROTO;
	case ESHUTDOWN:
	case ECANCELED:
		return -DER_CANCELED;
	case EIO:
	case ENOTCONN:
		return -DER_UNREACH;
	case ETIMEDOUT:
		return -DER_TIMEDOUT;
	case EOVERFLOW:
		return -DER_OVERFLOW;
	case EACCES:
		return -DER_NO_PERM;
	default:
		return -DER_INVAL;
	}
}

int
crt_native_cart_bulk_create(const struct crt_native_ops *ops, struct crt_native_ep *ep,
			    struct crt_native_bulk_session *session, unsigned capacity,
			    struct crt_native_cart_bulk **out)
{
	if (out == NULL)
		return -DER_INVAL;
	*out = NULL;
	if (!crt_native_bulk_session_on_endpoint(session, ops, ep) || capacity == 0 ||
	    capacity > 32768)
		return -DER_INVAL;
	struct crt_native_cart_bulk *b = calloc(1, sizeof(*b));
	if (b == NULL)
		return -DER_NOMEM;
	b->cache = calloc(capacity, sizeof(*b->cache));
	if (b->cache == NULL) {
		free(b);
		return -DER_NOMEM;
	}
	b->ops      = ops;
	b->ep       = ep;
	b->session  = session;
	b->capacity = capacity;
	atomic_init(&b->released, NULL);
	atomic_init(&b->remote_handles, 0);
	atomic_init(&b->closing, false);
	atomic_init(&b->error, 0);
	atomic_init(&b->active_operations, 0);
	atomic_init(&b->incoming, capacity);
	b->incoming_pending = capacity;
	b->operations       = calloc(capacity, sizeof(*b->operations));
	if (b->operations == NULL) {
		free(b->cache);
		free(b);
		return -DER_NOMEM;
	}
	b->pending_head = b->pending_tail = capacity;
	b->completed_head = b->completed_tail = capacity;
	for (unsigned i = 0; i < capacity; i++) {
		b->operations[i].next = i + 1;
		atomic_init(&b->operations[i].cancel, false);
		atomic_init(&b->operations[i].active, false);
	}
	pthread_mutex_lock(&registry_lock);
	b->registry_next = registry;
	registry         = b;
	pthread_mutex_unlock(&registry_lock);
	*out = b;
	return 0;
}

static int
evict(struct registration *r)
{
	if (r->native != NULL) {
		r->reclaiming = true;
		int rc        = crt_native_bulk_destroy(r->native);
		if (rc != 0)
			return result(rc);
		r->native = NULL;
	}
	if (r->original != NULL)
		crt_bulk_free(r->original);
	*r = (struct registration){0};
	return 0;
}

int
crt_native_cart_bulk_destroy(struct crt_native_cart_bulk *b)
{
	if (b == NULL)
		return -DER_INVAL;
	if (b->progressing || b->dispatching)
		return -DER_AGAIN;
	b->closing = true;
	crt_native_cart_bulk_progress(b);
	if (atomic_load(&b->remote_handles) || b->active_operations)
		return -DER_AGAIN;
	for (unsigned i = 0; i < b->capacity; i++) {
		int rc = evict(&b->cache[i]);
		if (rc != 0)
			return rc;
	}
	pthread_mutex_lock(&registry_lock);
	struct crt_native_cart_bulk **link = &registry;
	while (*link != b)
		link = &(*link)->registry_next;
	*link = b->registry_next;
	pthread_mutex_unlock(&registry_lock);
	free(b->operations);
	free(b->cache);
	free(b);
	return 0;
}

void
crt_native_cart_remote_acquire(struct crt_native_cart_remote *remote)
{
	atomic_fetch_add(&remote->binding->remote_handles, 1);
}

void
crt_native_cart_remote_release(struct crt_native_cart_remote *remote)
{
	struct crt_native_cart_bulk   *b    = remote->binding;
	struct crt_native_cart_remote *head = atomic_load(&b->released);
	do {
		remote->next = head;
	} while (!atomic_compare_exchange_weak(&b->released, &head, remote));
	/* The owner keeps remote_handles nonzero until it consumes this node. */
}

static int
progress_transfers(struct crt_native_cart_bulk *b);

int
crt_native_cart_bulk_progress(struct crt_native_cart_bulk *b)
{
	if (b == NULL)
		return -DER_INVAL;
	if (b->progressing)
		return -DER_AGAIN;
	b->progressing  = true;
	int transfer_rc = progress_transfers(b);
	for (unsigned budget = 0; budget < 32; budget++) {
		if (b->released_pending == NULL)
			b->released_pending = atomic_exchange(&b->released, NULL);
		struct crt_native_cart_remote *r = b->released_pending;
		if (r == NULL)
			break;
		int rc = crt_native_bulk_import_put(r->session, r->descriptor->lease_id);
		if (rc != 0) {
			b->progressing = false;
			return result(rc);
		}
		b->released_pending = r->next;
		crt_native_cart_destroy_remote(r);
		atomic_fetch_sub(&b->remote_handles, 1);
	}
	int rc         = crt_native_bulk_session_progress(b->session);
	b->progressing = false;
	if (transfer_rc != 0)
		return transfer_rc;
	return b->closing && rc == ESHUTDOWN ? 0 : result(rc);
}

static int
local_registration(struct crt_native_cart_bulk *b, crt_bulk_t handle, struct registration **out)
{
	if (b == NULL || handle == NULL || out == NULL)
		return -DER_INVAL;
	if (b->closing)
		return -DER_CANCELED;
	for (unsigned i = 0; i < b->capacity; i++) {
		struct registration *r = &b->cache[i];
		if (r->original == handle && !r->reclaiming) {
			*out = r;
			return 0;
		}
	}
	/* Bound registrations, including on cache churn; misses may reclaim only
	 * handles with no export/local DMA leases. Busy entries remain reachable.
	 */
	struct registration *r = NULL;
	for (unsigned budget = 0; budget < b->capacity; budget++) {
		struct registration *candidate = &b->cache[b->victim++ % b->capacity];
		if (evict(candidate) == 0) {
			r = candidate;
			break;
		}
	}
	if (r == NULL)
		return -DER_AGAIN;
	d_iov_t               iovs[CRT_NATIVE_BULK_MAX_SEGMENTS];
	d_sg_list_t           sgl = {.sg_nr = CRT_NATIVE_BULK_MAX_SEGMENTS, .sg_iovs = iovs};
	struct crt_native_iov native_iovs[CRT_NATIVE_BULK_MAX_SEGMENTS];
	int                   rc = crt_bulk_access(handle, &sgl);
	if (rc != 0)
		return rc; /* remote/forwarded handles cannot be re-registered */
	/* Deferred CaRT handles return their saved SGL by alias; Mercury fills
	 * the caller's SGL and its sg_nr_out. Accept both documented ownerships.
	 */
	unsigned count = sgl.sg_iovs == iovs ? sgl.sg_nr_out : sgl.sg_nr;
	if (sgl.sg_iovs == NULL || count == 0 || count > CRT_NATIVE_BULK_MAX_SEGMENTS)
		return -DER_INVAL;
	for (unsigned i = 0; i < count; i++)
		native_iovs[i] =
		    (struct crt_native_iov){sgl.sg_iovs[i].iov_buf, sgl.sg_iovs[i].iov_buf_len};
	/* Preserve the original CaRT permissions; the concrete wrapper supplies
	 * these without exposing Mercury registration internals to this module.
	 */
	crt_bulk_perm_t permission;
	rc = crt_native_cart_bulk_permission(handle, &permission);
	if (rc != 0)
		return rc;
	unsigned access = permission == CRT_BULK_RO   ? CRT_NATIVE_REMOTE_READ
			  : permission == CRT_BULK_WO ? CRT_NATIVE_REMOTE_WRITE
			  : permission == CRT_BULK_RW
			      ? CRT_NATIVE_REMOTE_READ | CRT_NATIVE_REMOTE_WRITE
			      : 0;
	if (access == 0)
		return -DER_INVAL;
	rc = crt_bulk_addref(handle);
	if (rc != 0)
		return rc;
	r->original = handle;
	r->count    = count;
	rc = crt_native_bulk_create(b->ops, b->ep, native_iovs, r->count, access, &r->native);
	if (rc != 0) {
		r->reclaiming = true;
		/* A rollback/deregistration failure must keep its handle and ref. */
		if (r->native == NULL)
			evict(r);
		return result(rc);
	}
	*out = r;
	return 0;
}

int
crt_native_cart_bulk_local(struct crt_native_cart_bulk *b, crt_bulk_t handle,
			   struct crt_native_bulk **out)
{
	if (out == NULL)
		return -DER_INVAL;
	struct registration *r;
	int                  rc = local_registration(b, handle, &r);
	if (rc == 0)
		*out = r->native;
	return rc;
}

int
crt_native_cart_bulk_proc(crt_proc_t proc, crt_proc_op_t op, crt_bulk_t *handle, void *arg)
{
	struct crt_native_cart_bulk_txn *txn    = arg;
	struct crt_native_proc          *cursor = crt_native_cart_cursor(proc);
	if (txn == NULL || txn->binding == NULL || cursor == NULL || handle == NULL)
		return -DER_INVAL;
	struct crt_native_cart_bulk *b = txn->binding;
	if (op == CRT_PROC_FREE) {
		if (*handle == NULL)
			return 0;
		int rc = crt_bulk_free(*handle);
		if (rc == 0)
			*handle = NULL;
		return rc;
	}
	if (b->closing)
		return -DER_CANCELED;
	if (op == CRT_PROC_DECODE) {
		struct crt_native_bulk_remote remote;
		int rc = result(crt_native_proc_bulk(cursor, NULL, &remote));
		if (rc != 0)
			return rc;
		*handle = NULL;
		if (remote.lease_id == 0)
			return 0;
		rc = result(crt_native_bulk_import(b->session, &remote));
		if (rc != 0)
			return rc;
		const struct crt_native_bulk_remote *stored;
		rc = result(crt_native_bulk_import_get(b->session, remote.lease_id, &stored));
		if (rc == 0) {
			/* Drop the lookup pin; import's original reference owns the wrapper. */
			crt_native_bulk_import_put(b->session, remote.lease_id);
			rc = crt_native_cart_wrap_remote(b, b->session, remote.lease_id, stored,
							 handle);
		}
		if (rc != 0)
			crt_native_bulk_import_put(b->session, remote.lease_id);
		return rc;
	}
	if (op != CRT_PROC_ENCODE)
		return -DER_INVAL;
	if (*handle == NULL)
		return result(crt_native_proc_bulk(cursor, NULL, NULL));
	if (txn->count == CRT_NATIVE_BULK_MAX_SEGMENTS)
		return -DER_OVERFLOW;
	struct registration *r;
	int                  rc = local_registration(b, *handle, &r);
	if (rc != 0)
		return rc;
	uint64_t id;
	rc = result(crt_native_bulk_export_begin(b->session, r->native, NULL, &id));
	if (rc != 0)
		return rc;
	uint32_t length = CRT_NATIVE_BULK_HEADER + CRT_NATIVE_BULK_SEGMENT * r->count;
	void    *view;
	size_t   used;
	rc = crt_native_proc_u32(cursor, &length);
	if (rc == 0)
		rc = crt_native_proc_span(cursor, length, &view);
	if (rc == 0)
		rc = crt_native_bulk_export_pack(b->session, id, view, length, &used);
	if (rc != 0) {
		crt_native_bulk_export_abort(b->session, id);
		return result(rc);
	}
	txn->exports[txn->count++] = id;
	return 0;
}

int
crt_native_cart_bulk_commit(struct crt_native_cart_bulk_txn *txn)
{
	if (txn == NULL || txn->binding == NULL)
		return -DER_INVAL;
	for (unsigned i = 0; i < txn->count; i++) {
		int rc = crt_native_bulk_export_publish(txn->binding->session, txn->exports[i]);
		if (rc != 0)
			return result(rc);
	}
	txn->count = 0;
	return 0;
}

int
crt_native_cart_bulk_abort(struct crt_native_cart_bulk_txn *txn)
{
	if (txn == NULL || txn->binding == NULL)
		return -DER_INVAL;
	while (txn->count) {
		int rc = crt_native_bulk_export_abort(txn->binding->session,
						      txn->exports[txn->count - 1]);
		if (rc != 0)
			return result(rc);
		txn->count--;
	}
	return 0;
}

int
crt_native_cart_bulk_transfer(struct crt_native_cart_bulk *b, struct crt_bulk_desc *desc,
			      crt_bulk_cb_t verify, crt_bulk_cb_t complete, void *arg,
			      crt_bulk_opid_t *id)
{
	if (b == NULL || desc == NULL || desc->bd_rpc == NULL || complete == NULL ||
	    verify == NULL || desc->bd_local_off < 0 || desc->bd_remote_off < 0 ||
	    desc->bd_len == 0 || desc->bd_local_hdl == NULL || desc->bd_remote_hdl == NULL ||
	    (desc->bd_bulk_op != CRT_BULK_GET && desc->bd_bulk_op != CRT_BULK_PUT))
		return -DER_INVAL;
	if (b->closing || b->error)
		return -DER_CANCELED;
	if (b->ops->post == NULL || b->ops->progress == NULL)
		return -DER_NOSYS;
	struct crt_native_cart_bulk         *remote_binding;
	const struct crt_native_bulk_remote *remote;
	int rc = crt_native_cart_remote_info(desc->bd_remote_hdl, &remote_binding, NULL, &remote);
	if (rc != 0 || remote_binding != b)
		return -DER_INVAL;
	/* Valid CaRT handles cannot fail addref. Keep both wrappers and the RPC
	 * through the completion callback, including accepted DMA error paths.
	 */
	crt_bulk_addref(desc->bd_local_hdl);
	crt_bulk_addref(desc->bd_remote_hdl);
	crt_req_addref(desc->bd_rpc);
	pthread_mutex_lock(&registry_lock);
	if (b->free_operation == b->capacity || b->closing) {
		rc = b->closing ? -DER_CANCELED : -DER_AGAIN;
		pthread_mutex_unlock(&registry_lock);
		crt_bulk_free(desc->bd_remote_hdl);
		crt_bulk_free(desc->bd_local_hdl);
		crt_req_decref(desc->bd_rpc);
		return rc;
	}
	unsigned               slot = b->free_operation;
	struct bulk_operation *op   = &b->operations[slot];
	b->free_operation           = op->next;
	op->descriptor              = *desc;
	op->verify                  = verify;
	op->complete                = complete;
	op->arg                     = arg;
	op->initialized             = false;
	op->setup_error             = 0;
	atomic_store(&op->cancel, false);
	op->active = true;
	b->active_operations++;
	pthread_mutex_unlock(&registry_lock);
	if (id != NULL)
		*id = op;
	unsigned head = atomic_load(&b->incoming);
	do {
		op->incoming_next = head;
	} while (!atomic_compare_exchange_weak(&b->incoming, &head, slot));
	return 0;
}

bool
crt_native_cart_bulk_try_abort(crt_context_t context, crt_bulk_opid_t id, int *rc)
{
	if (rc == NULL || id == NULL)
		return false;
	bool found = false;
	pthread_mutex_lock(&registry_lock);
	for (struct crt_native_cart_bulk *b = registry; b != NULL && !found; b = b->registry_next) {
		uintptr_t address = (uintptr_t)id, base = (uintptr_t)b->operations;
		if (address < base || address - base >= b->capacity * sizeof(*b->operations) ||
		    (address - base) % sizeof(*b->operations) != 0)
			continue;
		struct bulk_operation *op = id;
		found                     = true;
		if (!op->active)
			*rc = -DER_NONEXIST;
		else if (op->descriptor.bd_rpc->cr_ctx != context)
			*rc = -DER_INVAL;
		else {
			atomic_store(&op->cancel, true);
			*rc = 0;
		}
	}
	pthread_mutex_unlock(&registry_lock);
	return found;
}

static void
deliver_completion(struct crt_native_cart_bulk *b, unsigned slot)
{
	struct bulk_operation  *op   = &b->operations[slot];
	struct crt_bulk_cb_info info = {.bci_bulk_desc   = &op->descriptor,
					.bci_arg         = op->arg,
					.bci_rc          = op->final_result,
					.bci_complete_cb = op->complete};
	op->verify(&info);
	crt_bulk_free(op->descriptor.bd_remote_hdl);
	crt_bulk_free(op->descriptor.bd_local_hdl);
	crt_req_decref(op->descriptor.bd_rpc);
	pthread_mutex_lock(&registry_lock);
	op->next          = b->free_operation;
	b->free_operation = slot;
	pthread_mutex_unlock(&registry_lock);
	b->active_operations--;
}

int
crt_native_cart_bulk_defer_callbacks(struct crt_native_cart_bulk *b)
{
	if (b == NULL || b->active_operations)
		return -DER_INVAL;
	b->defer_callbacks = true;
	return 0;
}

unsigned
crt_native_cart_bulk_dispatch(struct crt_native_cart_bulk *b)
{
	if (b == NULL || b->progressing || b->dispatching)
		return 0;
	b->dispatching = true;
	unsigned count = 0;
	while (count < 32 && b->completed_head != b->capacity) {
		unsigned slot     = b->completed_head;
		b->completed_head = b->operations[slot].next;
		if (b->completed_head == b->capacity)
			b->completed_tail = b->capacity;
		deliver_completion(b, slot);
		count++;
	}
	b->dispatching = false;
	return count;
}

static int
progress_transfers(struct crt_native_cart_bulk *b)
{
	if (b->active_operations == 0)
		return result(b->error);
	for (unsigned budget = 0; budget < 32; budget++) {
		if (b->incoming_pending == b->capacity)
			b->incoming_pending = atomic_exchange(&b->incoming, b->capacity);
		if (b->incoming_pending == b->capacity)
			break;
		unsigned               slot = b->incoming_pending;
		struct bulk_operation *op   = &b->operations[slot];
		b->incoming_pending         = op->incoming_next;
		op->next                    = b->capacity;
		if (b->pending_tail != b->capacity)
			b->operations[b->pending_tail].next = slot;
		else
			b->pending_head = slot;
		b->pending_tail = slot;
	}
	struct crt_native_completion completions[32];
	unsigned                     count = 0;
	int                          rc    = b->ops->progress(b->ep, completions, 32, &count);
	if (rc != 0 && b->error == 0)
		b->error = rc;
	for (unsigned i = 0; i < count; i++) {
		uintptr_t address = (uintptr_t)completions[i].cookie,
			  base    = (uintptr_t)b->operations;
		if (address < base || address - base >= b->capacity * sizeof(*b->operations)) {
			b->error = EPROTO;
			continue;
		}
		struct bulk_operation *op =
		    &b->operations[(address - base) / sizeof(*b->operations)];
		if (!op->active ||
		    crt_native_bulk_transfer_complete(&op->transfer, &completions[i]) != 0)
			b->error = EPROTO;
		if (completions[i].error && b->error == 0)
			b->error = completions[i].error;
	}
	if (b->error)
		crt_native_bulk_session_shutdown(b->session);
	for (unsigned budget = 0; budget < 32 && b->pending_head != b->capacity; budget++) {
		unsigned               slot = b->pending_head;
		struct bulk_operation *op   = &b->operations[slot];
		b->pending_head             = op->next;
		if (b->pending_head == b->capacity)
			b->pending_tail = b->capacity;
		bool expired = crt_native_cart_bulk_expired(&op->descriptor);
		if (expired && op->setup_error == 0)
			op->setup_error = -DER_TIMEDOUT;
		bool canceled = atomic_load(&op->cancel) || b->closing || b->error || expired;
		if (!op->initialized && !op->setup_error) {
			if (canceled)
				op->setup_error = -DER_CANCELED;
			else {
				struct crt_native_bulk              *local;
				const struct crt_native_bulk_remote *remote;
				op->setup_error = crt_native_cart_bulk_local(
				    b, op->descriptor.bd_local_hdl, &local);
				if (!op->setup_error)
					op->setup_error = crt_native_cart_remote_info(
					    op->descriptor.bd_remote_hdl, NULL, NULL, &remote);
				if (!op->setup_error) {
					op->setup_error = result(crt_native_bulk_transfer_init(
					    &op->transfer, b->ops, b->ep, local, remote,
					    op->descriptor.bd_bulk_op == CRT_BULK_GET
						? CRT_NATIVE_READ
						: CRT_NATIVE_WRITE,
					    op->descriptor.bd_local_off,
					    op->descriptor.bd_remote_off, op->descriptor.bd_len));
					op->initialized = op->setup_error == 0;
				}
			}
		}
		if (op->initialized) {
			if (canceled)
				crt_native_bulk_transfer_cancel(&op->transfer);
			else
				crt_native_bulk_transfer_post(&op->transfer);
		}
		int status = op->initialized ? crt_native_bulk_transfer_status(&op->transfer) : 0;
		if (status == EINPROGRESS) {
			op->next = b->capacity;
			if (b->pending_tail != b->capacity)
				b->operations[b->pending_tail].next = slot;
			else
				b->pending_head = slot;
			b->pending_tail = slot;
			continue;
		}
		if (op->initialized)
			crt_native_bulk_transfer_fini(&op->transfer);
		pthread_mutex_lock(&registry_lock);
		op->active = false;
		pthread_mutex_unlock(&registry_lock);
		op->final_result = b->error          ? result(b->error)
				   : op->setup_error ? op->setup_error
						     : result(status);
		if (b->defer_callbacks) {
			op->next = b->capacity;
			if (b->completed_tail != b->capacity)
				b->operations[b->completed_tail].next = slot;
			else
				b->completed_head = slot;
			b->completed_tail = slot;
		} else
			deliver_completion(b, slot);
	}
	return result(b->error);
}
